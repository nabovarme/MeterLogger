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

static char ota_host[64];
static char ota_path[128];
static int ota_port = 80;

// Verifies the native ESP8266 firmware XOR checksum directly from flash memory
ICACHE_FLASH_ATTR
static bool verify_esp_image(uint32_t addr) {
	uint32_t header[2];
	uint8_t magic;
	uint8_t segments;
	uint32_t offset;
	uint8_t checksum;
	int i, j;
	uint32_t seg_header[2];
	uint32_t seg_size;
	uint32_t left;
	uint32_t buf[16];
	uint32_t to_read;
	uint32_t read_len;
	uint8_t *byte_buf;
	uint32_t padded_offset;
	uint32_t last_word;
	uint8_t file_checksum;

	if (spi_flash_read(addr, header, 8) != 0) {
		return false;
	}
	
	magic = header[0] & 0xFF;
	if (magic != 0xE9 && magic != 0xEA) {
		return false;
	}

	segments = (header[0] >> 8) & 0xFF;
	if (segments == 0 || segments > 16) {
		return false; // Sanity check limits
	}
	
	offset = 8;
	checksum = 0xEF; // ESP8266 checksum seed
	
	for (i = 0; i < segments; i++) {
		if (spi_flash_read(addr + offset, seg_header, 8) != 0) {
			return false;
		}
		
		seg_size = seg_header[1];
		if (seg_size > 0x100000) {
			return false; // Sanity check: >1MB segment
		}
		offset += 8;
		
		left = seg_size;
		while (left > 0) {
			to_read = (left > 64) ? 64 : left;
			// spi_flash_read requires lengths to be multiples of 4
			read_len = (to_read + 3) & ~3;
			if (spi_flash_read(addr + offset, buf, read_len) != 0) {
				return false;
			}
			
			byte_buf = (uint8_t *)buf;
			for (j = 0; j < to_read; j++) {
				checksum ^= byte_buf[j];
			}
			
			offset += to_read;
			left -= to_read;
		}
	}
	
	// Firmware files are padded with nulls up to a 16-byte boundary.
	// The final checksum byte is located at the very end of that padded block.
	padded_offset = offset + 16 - (offset % 16);
	if (spi_flash_read(addr + padded_offset - 4, &last_word, 4) != 0) {
		return false;
	}
	
	file_checksum = (last_word >> 24) & 0xFF;
	
#ifdef DEBUG
	os_printf("OTA Checksum verify: calculated=0x%02X, file=0x%02X\n", checksum, file_checksum);
#endif
	
	return (checksum == file_checksum);
}

ICACHE_FLASH_ATTR
static void ota_tcp_recv_cb(void *arg, char *pusrdata, unsigned short length) {
	char *pdata;
	uint16_t len;
	char *body;
	uint8_t magic_byte;
	rboot_config conf;
	uint32_t target_addr;
	char *cl;

	pdata = pusrdata;
	len = length;

	if (!headers_parsed) {
		// Check for 200 OK status code first
		if (os_strstr(pdata, "HTTP/1.1 200 OK") == NULL && os_strstr(pdata, "HTTP/1.0 200 OK") == NULL) {
#ifdef DEBUG
			os_printf("OTA Error: Non-200 HTTP response received!\n");
#endif
			if (ota_mqtt_client) {
				mqtt_rpc_ota_status(ota_mqtt_client, "error_http_not_200");
			}
			espconn_disconnect(&ota_conn);
			return;
		}

		// Extract Content-Length for size validation
		cl = (char *)os_strstr(pdata, "Content-Length: ");
		if (!cl) {
			cl = (char *)os_strstr(pdata, "content-length: ");
		}
		if (cl) {
			ota_content_length = atoi(cl + 16);
		}

		// Find the end of the HTTP headers
		body = (char *)os_strstr(pdata, "\r\n\r\n");
		if (body) {
			headers_parsed = true;
			body += 4; // Skip past the \r\n\r\n
			len -= (body - pdata);
			pdata = body;
		} else {
			// Still waiting for the end of headers
			return;
		}
	}

	// We are in the body. If we haven't validated the binary yet, do it now.
	if (headers_parsed && !is_valid_binary && len > 0) {
		magic_byte = (uint8_t)pdata[0];
		
		// 0xE9 is the standard ESP8266 image magic byte; 0xEA is the v2 header
		if (magic_byte != 0xE9 && magic_byte != 0xEA) {
#ifdef DEBUG
			os_printf("OTA Error: Invalid magic byte (0x%02X). Expected 0xE9!\n", magic_byte);
			if (magic_byte == '<') { // 0x3C
				os_printf("OTA Error: Received HTML instead of firmware binary.\n");
			}
#endif
			if (ota_mqtt_client) {
				mqtt_rpc_ota_status(ota_mqtt_client, "error_invalid_magic");
			}
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
	if (is_valid_binary && len > 0) {
		if (!rboot_write_flash(&ota_status, (uint8_t *)pdata, len)) {
#ifdef DEBUG
			os_printf("OTA: Flash write failed. Disconnecting.\n");
#endif
			if (ota_mqtt_client) {
				mqtt_rpc_ota_status(ota_mqtt_client, "error_flash_write");
			}
			espconn_disconnect(&ota_conn);
		} else {
			ota_received_bytes += len;
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
	char status_msg[32]; // C89 compliant declaration at the top

	// Only finalize and reboot if the file was a valid binary
	if (is_valid_binary) {
		rboot_write_end(&ota_status);
		
		// 1. Verify Content-Length if it was provided by the HTTP server
		if (ota_content_length > 0 && ota_received_bytes != ota_content_length) {
#ifdef DEBUG
			os_printf("OTA Error: Download incomplete. Received %u of %u bytes.\n", ota_received_bytes, ota_content_length);
#endif
			if (ota_mqtt_client) {
				mqtt_rpc_ota_status(ota_mqtt_client, "error_truncated");
			}
			ota_in_progress = false; // Release the lock
			return;
		}
		
		// 2. Verify Native ESP8266 Firmware Checksum
		if (!verify_esp_image(rboot_get_config().roms[ota_target_rom])) {
#ifdef DEBUG
			os_printf("OTA Error: Firmware checksum verification failed!\n");
#endif
			if (ota_mqtt_client) {
				mqtt_rpc_ota_status(ota_mqtt_client, "error_checksum");
			}
			ota_in_progress = false; // Release the lock
			return;
		}

		rboot_set_current_rom(ota_target_rom);
#ifdef DEBUG
		os_printf("OTA: Download and checksum complete. Rebooting to rom %d...\n", ota_target_rom);
#endif
		if (ota_mqtt_client) {
			tfp_snprintf(status_msg, sizeof(status_msg), "success&target_rom=%d", ota_target_rom + 1);
			mqtt_rpc_ota_status(ota_mqtt_client, status_msg);
		}
		
		// Keep the native reboot call strictly where it was
		system_restart_defered();
	} else {
#ifdef DEBUG
		os_printf("OTA: Disconnected before valid download completed.\n");
#endif
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
	} else {
		strcpy(ota_path, "/");
	}

	// Extract host and port
	if (colon && (!slash || colon < slash)) {
		strncpy(ota_host, p, colon - p);
		ota_host[colon - p] = '\0';
		ota_port = atoi(colon + 1);
	} else {
		if (slash) {
			strncpy(ota_host, p, slash - p);
			ota_host[slash - p] = '\0';
		} else {
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
