#include <esp8266.h>
#include <string.h>
#include <stdlib.h>
#include "ota.h"
#include "rboot-api.h"
#include "tinyprintf.h"
#include "utils.h"

#include <lwip/ip.h>
#include <lwip/tcp_impl.h>
#include <lwip/dns.h>
#include <espconn.h>
#include "spi_flash.h"
#include "mqtt.h"
#include "mqtt_rpc.h"
#include "utils.h"

#define OTA_REBOOT_DELAY_MS    6000
#define ESP_IMAGE_MAGIC_V1     0xE9
#define ESP_IMAGE_MAGIC_V2     0xEA
#define OTA_DEFAULT_PORT       80
#define OTA_MAX_HEADER_SIZE    1024
#define MIN_FIRMWARE_SIZE      102400 // 100 KB minimum sane size for firmware binary

// Bring in the lock from mqtt_rpc.c so we can unlock on failure
extern bool ota_in_progress;
static MQTT_Client *ota_mqtt_client = NULL;

static struct espconn ota_conn;
static esp_tcp ota_tcp;
static ip_addr_t ota_ip;
static rboot_write_status ota_status;
static uint8_t ota_target_rom;
static bool headers_parsed = false;
static bool is_valid_binary = false;

static uint32_t ota_content_length = 0;
static uint32_t ota_received_bytes = 0;

static char ota_header_buffer[OTA_MAX_HEADER_SIZE];
static uint16_t ota_header_len = 0;

static char ota_host[64];
static char ota_path[128];
static int ota_port = OTA_DEFAULT_PORT;

static os_timer_t ota_reboot_timer;

ICACHE_FLASH_ATTR
static void ota_reboot_timer_cb(void *arg) {
	system_restart_defered();
}

ICACHE_FLASH_ATTR
static void ota_tcp_recv_cb(void *arg, char *pusrdata, unsigned short length) {
	char *pdata;
	uint16_t len;
	char *body_marker;
	uint8_t magic_byte;
	rboot_config conf;
	uint32_t target_addr;
	char *cl;
	static uint8_t last_reported_progress = 0;
	uint8_t current_progress = 0;
	char progress_msg[32];
	uint16_t copy_len;
	uint16_t total_header_size;
	
	/* Word-aligned buffer for initial unaligned payload block */
	uint32_t aligned_buf[128 / 4];
	uint8_t *aligned_pdata;

	pdata = pusrdata;
	len = length;

	if (!headers_parsed) {
		// Buffer incoming data to assemble a complete HTTP header
		copy_len = length;
		if (ota_header_len + copy_len > OTA_MAX_HEADER_SIZE - 1) {
			copy_len = OTA_MAX_HEADER_SIZE - 1 - ota_header_len;
		}
		
		os_memcpy(ota_header_buffer + ota_header_len, pusrdata, copy_len);
		ota_header_len += copy_len;
		ota_header_buffer[ota_header_len] = '\0';
		
		body_marker = (char *)os_strstr(ota_header_buffer, "\r\n\r\n");
		if (body_marker) {
			// Validate HTTP status now that the whole header is accumulated
			if (os_strstr(ota_header_buffer, "200 OK") == NULL && os_strstr(ota_header_buffer, "200") == NULL) {
#ifdef DEBUG
				os_printf("OTA Error: Non-200 HTTP response received!\n");
#endif
				if (ota_mqtt_client) {
					mqtt_rpc_ota_status(ota_mqtt_client, "error_http_not_200");
				}
				ota_in_progress = false;
				espconn_disconnect(&ota_conn);
				return;
			}

			// Extract Content-Length for size validation
			cl = (char *)os_strstr(ota_header_buffer, "Content-Length: ");
			if (!cl) {
				cl = (char *)os_strstr(ota_header_buffer, "content-length: ");
			}
			if (cl) {
				ota_content_length = atoi(cl + 16);
			}

			headers_parsed = true;
			last_reported_progress = 0; // Reset progress tracker
			
			// Calculate how much of the CURRENT payload is actually binary data
			total_header_size = (body_marker + 4) - ota_header_buffer;
			
			if (ota_header_len > total_header_size) {
				len = ota_header_len - total_header_size;
				pdata = pusrdata + (length - len); // Offset into the current packet
				
				// Ensure strict 32-bit alignment by copying initial slice into stack buffer
				if (len > sizeof(aligned_buf)) {
					copy_len = sizeof(aligned_buf);
				} else {
					copy_len = len;
				}
				aligned_pdata = (uint8_t *)aligned_buf;
				os_memcpy(aligned_pdata, pdata, copy_len);
				pdata = (char *)aligned_pdata;
			} else {
				len = 0;
			}
		} else {
			if (ota_header_len >= OTA_MAX_HEADER_SIZE - 1) {
#ifdef DEBUG
				os_printf("OTA Error: Header too large / No empty line found.\n");
#endif
				if (ota_mqtt_client) {
					mqtt_rpc_ota_status(ota_mqtt_client, "error_header_overflow");
				}
				ota_in_progress = false;
				espconn_disconnect(&ota_conn);
			}
			return; // Still waiting for the rest of the header
		}
	}

	// Validate magic byte on the very first binary chunk
	if (headers_parsed && !is_valid_binary && len > 0) {
		magic_byte = (uint8_t)pdata[0];
		
		// 0xE9 is the standard ESP8266 image magic byte; 0xEA is the v2 header
		if (magic_byte != ESP_IMAGE_MAGIC_V1 && magic_byte != ESP_IMAGE_MAGIC_V2) {
#ifdef DEBUG
			os_printf("OTA Error: Invalid magic byte (0x%02X). Expected 0x%02X or 0x%02X!\n", 
			          magic_byte, ESP_IMAGE_MAGIC_V1, ESP_IMAGE_MAGIC_V2);
			if (magic_byte == '<') { // 0x3C
				os_printf("OTA Error: Received HTML instead of firmware binary.\n");
			}
#endif
			if (ota_mqtt_client) {
				mqtt_rpc_ota_status(ota_mqtt_client, "error_invalid_magic");
			}
			ota_in_progress = false;
			espconn_disconnect(&ota_conn);
			return;
		}

		// Valid binary detected! Safe to initialize flash erasure.
		is_valid_binary = true;

		// Look up the exact flash memory address for the target rom slot
		conf = rboot_get_config();
		target_addr = conf.roms[ota_target_rom];
		
		// Initialize the rboot flash writing engine
		ota_status = rboot_write_init(target_addr);
#ifdef DEBUG
		os_printf("OTA: Validation Passed! Flashing target slot %d at 0x%08X\n", ota_target_rom, target_addr);
#endif
	}

	// Write incoming payload chunks directly to flash memory
	// rboot_write_flash automatically handles unaligned chunk sizes buffering
	if (is_valid_binary && len > 0) {
		if (!rboot_write_flash(&ota_status, (uint8_t *)pdata, len)) {
#ifdef DEBUG
			os_printf("OTA: Flash write failed. Disconnecting.\n");
#endif
			if (ota_mqtt_client) {
				mqtt_rpc_ota_status(ota_mqtt_client, "error_flash_write");
			}
			is_valid_binary = false;
			ota_in_progress = false;
			espconn_disconnect(&ota_conn);
		}
		else {
			ota_received_bytes += len;

			// Send periodic MQTT progress reports every 10%
			if (ota_content_length > 0) {
				current_progress = (uint8_t)((ota_received_bytes * 100) / ota_content_length);

				if (current_progress >= last_reported_progress + 10 && current_progress < 100) {
					last_reported_progress = (current_progress / 10) * 10;
					if (ota_mqtt_client) {
						tfp_snprintf(progress_msg, sizeof(progress_msg), "flashing_%d%%", last_reported_progress);
						mqtt_rpc_ota_status(ota_mqtt_client, progress_msg);
					}
				}
			}
		}
	}
}

ICACHE_FLASH_ATTR
static void ota_tcp_recon_cb(void *arg, sint8 err) {
#ifdef DEBUG
	os_printf("OTA: TCP network error (%d). Aborting.\n", err);
#endif
	if (ota_mqtt_client) {
		mqtt_rpc_ota_status(ota_mqtt_client, "error_tcp_drop");
	}
	ota_in_progress = false; // Release lock on abnormal network drop
}

ICACHE_FLASH_ATTR
static void ota_tcp_discon_cb(void *arg) {
	char status_msg[32];

	// Only finalize and reboot if the file was a valid binary
	if (is_valid_binary) {
		rboot_write_end(&ota_status);
		
		// 1. Enforce minimum received byte check to prevent soft-bricking on truncated downloads
		if (ota_received_bytes < MIN_FIRMWARE_SIZE) {
#ifdef DEBUG
			os_printf("OTA Error: Downloaded size too small (%u bytes < %u bytes limit).\n", ota_received_bytes, MIN_FIRMWARE_SIZE);
#endif
			if (ota_mqtt_client) {
				mqtt_rpc_ota_status(ota_mqtt_client, "error_too_small");
			}
			ota_in_progress = false;
			return;
		}

		// 2. Verify Content-Length if it was provided by the HTTP server
		if (ota_content_length > 0 && ota_received_bytes != ota_content_length) {
#ifdef DEBUG
			os_printf("OTA Error: Download incomplete. Received %u of %u bytes.\n", ota_received_bytes, ota_content_length);
#endif
			if (ota_mqtt_client) {
				mqtt_rpc_ota_status(ota_mqtt_client, "error_truncated");
			}
			ota_in_progress = false; // Release the lock on failure
			return;
		}
		
		// 3. Stage a temporary boot to the target slot.
		// If the new binary crashes or fails to connect/pass self-test,
		// rBoot will automatically roll back to the old working slot on next reset.
		rboot_set_temp_rom(ota_target_rom);

#ifdef DEBUG
		os_printf("OTA: Download complete. Staging temp boot to rom %d...\n", ota_target_rom);
#endif
		if (ota_mqtt_client) {
			tfp_snprintf(status_msg, sizeof(status_msg), "success&target_rom=%d", ota_target_rom + 1);
			mqtt_rpc_ota_status(ota_mqtt_client, status_msg);
			
			// Send a follow-up message with the exact countdown dynamically calculated
			tfp_snprintf(status_msg, sizeof(status_msg), "restarting_in_%ds", (OTA_REBOOT_DELAY_MS + SYSTEM_RESTART_DELAY_MS) / 1000);
			mqtt_rpc_ota_status(ota_mqtt_client, status_msg);
		}
		
		// Delay reboot by 6 seconds to allow the MQTT stack to flush its queue 
		// over the network before the Wi-Fi radio shuts down.
		os_timer_disarm(&ota_reboot_timer);
		os_timer_setfn(&ota_reboot_timer, (os_timer_func_t *)ota_reboot_timer_cb, NULL);
		os_timer_arm(&ota_reboot_timer, OTA_REBOOT_DELAY_MS, 0);
	}
	else {
#ifdef DEBUG
		os_printf("OTA: Disconnected before valid download completed.\n");
#endif
		if (ota_mqtt_client) {
			mqtt_rpc_ota_status(ota_mqtt_client, "error_disconnected");
		}
		ota_in_progress = false; // Release the lock on failure
	}
}

ICACHE_FLASH_ATTR
static void ota_tcp_connect_cb(void *arg) {
	char request[512];
	
	// Format the HTTP GET request. Using HTTP/1.0 ensures the server
	// closes the connection automatically when the file finishes downloading.
	tfp_snprintf(request, sizeof(request),
		"GET %s HTTP/1.0\r\n"
		"Host: %s\r\n"
		"Connection: close\r\n"
		"User-Agent: ESP8266-OTA\r\n"
		"\r\n", ota_path, ota_host);

	espconn_send(&ota_conn, (uint8_t *)request, strlen(request));
}

ICACHE_FLASH_ATTR
static void ota_dns_found_cb(const char *name, ip_addr_t *ipaddr, void *arg) {
	if (ipaddr == NULL) {
#ifdef DEBUG
		os_printf("OTA: DNS resolution failed.\n");
#endif
		if (ota_mqtt_client) {
			mqtt_rpc_ota_status(ota_mqtt_client, "error_dns");
		}
		ota_in_progress = false; // Release lock on failure
		return;
	}

	os_memcpy(ota_conn.proto.tcp->remote_ip, &ipaddr->addr, 4);
	ota_conn.proto.tcp->remote_port = ota_port;
	ota_conn.proto.tcp->local_port = espconn_port();

	espconn_regist_connectcb(&ota_conn, ota_tcp_connect_cb);
	espconn_regist_recvcb(&ota_conn, ota_tcp_recv_cb);
	espconn_regist_disconcb(&ota_conn, ota_tcp_discon_cb);
	espconn_regist_reconcb(&ota_conn, ota_tcp_recon_cb);

	espconn_connect(&ota_conn);
}

ICACHE_FLASH_ATTR
bool start_ota_upgrade(MQTT_Client *client, const char *url, uint8_t *out_target_rom) {
	const char *p;
	const char *slash;
	const char *colon;
	char ota_url[256];
	uint8_t current_rom;
	err_t err;

	ota_mqtt_client = client;

	current_rom = rboot_get_current_rom();
	ota_target_rom = (current_rom == 0) ? 1 : 0;
	
	if (out_target_rom) {
		*out_target_rom = ota_target_rom;
	}
	
	ota_content_length = 0;
	ota_received_bytes = 0;

	// Use exact full URL constructed by mqtt_rpc.c
	strncpy(ota_url, url, sizeof(ota_url) - 1);
	ota_url[sizeof(ota_url) - 1] = '\0';

#ifdef DEBUG
	os_printf("OTA: Request URL: %s\n", ota_url);
#endif

	// 1. Parse host, port, and full path (including query string)
	p = ota_url;
	if (strncmp(p, "http://", 7) == 0) {
		p += 7;
	}

	slash = strchr(p, '/');
	colon = strchr(p, ':');

	// Extract path
	if (slash) {
		strncpy(ota_path, slash, sizeof(ota_path) - 1);
	}
	else {
		strcpy(ota_path, "/");
	}

	// Extract host and port
	if (colon && (!slash || colon < slash)) {
		strncpy(ota_host, p, colon - p);
		ota_host[colon - p] = '\0';
		ota_port = atoi(colon + 1);
	}
	else {
		if (slash) {
			strncpy(ota_host, p, slash - p);
			ota_host[slash - p] = '\0';
		}
		else {
			strncpy(ota_host, p, sizeof(ota_host) - 1);
		}
		ota_port = 80;
	}

#ifdef DEBUG
	os_printf("OTA: Parsed Host: %s | Port: %d | Path: %s\n", ota_host, ota_port, ota_path);
	os_printf("OTA: HTTP GET Request:\nGET %s HTTP/1.0\r\nHost: %s\r\n\n", ota_path, ota_host);
#endif

	// 2. Setup the TCP connection
	headers_parsed = false;
	is_valid_binary = false;
	ota_header_len = 0;
	memset(&ota_conn, 0, sizeof(ota_conn));
	memset(&ota_tcp, 0, sizeof(ota_tcp));
	ota_conn.type = ESPCONN_TCP;
	ota_conn.state = ESPCONN_NONE;
	ota_conn.proto.tcp = &ota_tcp;

	// 3. Resolve DNS (or connect immediately if it's an IP)
	err = dns_gethostbyname(ota_host, &ota_ip, ota_dns_found_cb, &ota_conn);
	if (err == ERR_OK) {
		ota_dns_found_cb(ota_host, &ota_ip, &ota_conn);
	}

	return true;
}
