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
#include "crypto/sha256.h"

#define OTA_MAX_RETRIES        5       // Maximum HTTP chunk/connection retries
#define OTA_RECONNECT_DELAY_MS 2000    // 2s backoff delay before reconnecting
#define SPI_SECTOR_SIZE        4096    // 4KB flash erase sector boundary

#define OTA_REBOOT_DELAY_MS    6000
#define ESP_IMAGE_MAGIC_V1     0xE9
#define ESP_IMAGE_MAGIC_V2     0xEA
#define OTA_DEFAULT_PORT       80
#define OTA_MAX_HEADER_SIZE    1024
#define MIN_FIRMWARE_SIZE      102400  // 100 KB minimum sane size
#define MAX_FIRMWARE_SIZE      0x69000 // 420 KB max safe partition size
#define OTA_NETWORK_TIMEOUT_MS 15000   // 15s inactivity timeout

// Bring in the lock from mqtt_rpc.c so we can unlock on failure
extern bool ota_in_progress;
static MQTT_Client *ota_mqtt_client = NULL;

static struct espconn ota_conn;
static esp_tcp ota_tcp;
static ip_addr_t ota_ip;
static rboot_write_status ota_status;
static uint8_t ota_target_rom;
static uint32_t ota_target_base_addr = 0;

static bool headers_parsed = false;
static bool is_valid_binary = false;
static bool is_partial_content = false;

static uint32_t ota_content_length = 0;
static uint32_t ota_received_bytes = 0;
static uint8_t ota_retry_count = 0;

static char ota_header_buffer[OTA_MAX_HEADER_SIZE];
static uint16_t ota_header_len = 0;

static char ota_host[64];
static char ota_path[128];
static int ota_port = OTA_DEFAULT_PORT;

static os_timer_t ota_reboot_timer;
static os_timer_t ota_timeout_timer;
static os_timer_t ota_retry_timer;

// SHA-256 state
static sha256_ctx_t ota_sha_ctx;
static uint8_t expected_sha256_1[32];
static uint8_t expected_sha256_2[32];
static bool has_sha256_check_1 = false;
static bool has_sha256_check_2 = false;

static void ota_trigger_reconnect(void);
static void ota_dns_found_cb(const char *name, ip_addr_t *ipaddr, void *arg);

ICACHE_FLASH_ATTR
static void ota_reboot_timer_cb(void *arg) {
	system_restart_defered();
}

ICACHE_FLASH_ATTR
static void ota_cleanup_timers(void) {
	os_timer_disarm(&ota_timeout_timer);
	os_timer_disarm(&ota_retry_timer);
}

ICACHE_FLASH_ATTR
static void ota_timeout_timer_cb(void *arg) {
	if (ota_in_progress) {
#ifdef DEBUG
		os_printf("[OTA] Error: Download stalled/timed out! Aborting connection...\n");
#endif
		espconn_abort(&ota_conn);
	}
}

// Re-synchronize SHA-256 hash stream from flash up to current ota_received_bytes
ICACHE_FLASH_ATTR
static void ota_rehash_flash(void) {
	uint32_t addr;
	uint32_t chunk_len;
	uint8_t buffer[256];

	if (!has_sha256_check_1 && !has_sha256_check_2) return;

	sha256_init(&ota_sha_ctx);
	addr = ota_target_base_addr;

	while (addr < ota_target_base_addr + ota_received_bytes) {
		chunk_len = sizeof(buffer);
		if (addr + chunk_len > ota_target_base_addr + ota_received_bytes) {
			chunk_len = (ota_target_base_addr + ota_received_bytes) - addr;
		}
		spi_flash_read(addr, (uint32_t *)buffer, chunk_len);
		sha256_update(&ota_sha_ctx, buffer, chunk_len);
		addr += chunk_len;
	}
}

ICACHE_FLASH_ATTR
static void ota_retry_timer_cb(void *arg) {
	err_t err;

	headers_parsed = false;
	is_valid_binary = (ota_received_bytes > 0);
	ota_header_len = 0;

	memset(&ota_conn, 0, sizeof(ota_conn));
	memset(&ota_tcp, 0, sizeof(ota_tcp));
	ota_conn.type = ESPCONN_TCP;
	ota_conn.state = ESPCONN_NONE;
	ota_conn.proto.tcp = &ota_tcp;

	os_timer_disarm(&ota_timeout_timer);
	os_timer_setfn(&ota_timeout_timer, (os_timer_func_t *)ota_timeout_timer_cb, NULL);
	os_timer_arm(&ota_timeout_timer, OTA_NETWORK_TIMEOUT_MS, 0);

	err = dns_gethostbyname(ota_host, &ota_ip, ota_dns_found_cb, &ota_conn);
	if (err == ERR_OK) {
		ota_dns_found_cb(ota_host, &ota_ip, &ota_conn);
	}
}

ICACHE_FLASH_ATTR
static void ota_trigger_reconnect(void) {
	char status_buf[64];

	if (ota_retry_count < OTA_MAX_RETRIES && ota_in_progress) {
		ota_retry_count++;

		// Rewind received bytes to nearest 4KB SPI flash sector boundary
		ota_received_bytes &= ~(SPI_SECTOR_SIZE - 1);

#ifdef DEBUG
		os_printf("[OTA] Network drop! Attempting resume %d/%d from byte offset %u...\n", ota_retry_count, OTA_MAX_RETRIES, ota_received_bytes);
#endif
		if (ota_mqtt_client) {
			tfp_snprintf(status_buf, sizeof(status_buf), "retrying_chunk_%d_offset_%u", ota_retry_count, ota_received_bytes);
			mqtt_rpc_ota_status(ota_mqtt_client, status_buf);
		}

		os_timer_disarm(&ota_retry_timer);
		os_timer_setfn(&ota_retry_timer, (os_timer_func_t *)ota_retry_timer_cb, NULL);
		os_timer_arm(&ota_retry_timer, OTA_RECONNECT_DELAY_MS, 0);
	} else {
#ifdef DEBUG
		os_printf("[OTA] ERROR: Max retries (%d) exceeded or aborted!\n", OTA_MAX_RETRIES);
#endif
		ota_cleanup_timers();
		if (ota_mqtt_client) {
			mqtt_rpc_ota_status(ota_mqtt_client, "error_tcp_drop");
		}
		is_valid_binary = false;
		ota_in_progress = false;
	}
}

ICACHE_FLASH_ATTR
static void ota_tcp_recv_cb(void *arg, char *pusrdata, unsigned short length) {
	char *pdata;
	uint16_t len;
	char *body_marker;
	uint8_t magic_byte;
	rboot_config conf;
	uint32_t current_flash_addr;
	uint32_t sector_to_erase;
	char *cl;
	static uint8_t last_reported_progress = 0;
	uint8_t current_progress = 0;
	char progress_msg[32];
	uint16_t copy_len;
	uint16_t total_header_size;

	// Reset network inactivity window on every packet received
	os_timer_disarm(&ota_timeout_timer);
	os_timer_arm(&ota_timeout_timer, OTA_NETWORK_TIMEOUT_MS, 0);

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
			if (os_strstr(ota_header_buffer, "206") != NULL) {
				is_partial_content = true;
			} else if (os_strstr(ota_header_buffer, "200 OK") != NULL || os_strstr(ota_header_buffer, "200") != NULL) {
				is_partial_content = false;
				// Server ignored Range header and sent full file; reset offset to 0
				ota_received_bytes = 0;
			} else {
#ifdef DEBUG
				os_printf("[OTA] ERROR: Non-200/206 HTTP response received!\n");
#endif
				ota_cleanup_timers();
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
			if (cl && ota_received_bytes == 0) {
				ota_content_length = atoi(cl + 16);
#ifdef DEBUG
				os_printf("[OTA] Firmware size: %u bytes\n", ota_content_length);
#endif
			}

			headers_parsed = true;
			last_reported_progress = (uint8_t)((ota_received_bytes * 100) / (ota_content_length > 0 ? ota_content_length : 1));
			
			total_header_size = (body_marker + 4) - ota_header_buffer;
			
			if (ota_header_len > total_header_size) {
				len = ota_header_len - total_header_size;
				pdata = pusrdata + (length - len);
			} else {
				len = 0;
			}
		} else {
			if (ota_header_len >= OTA_MAX_HEADER_SIZE - 1) {
#ifdef DEBUG
				os_printf("[OTA] ERROR: Header overflow!\n");
#endif
				ota_cleanup_timers();
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
		if (ota_received_bytes == 0 && magic_byte != ESP_IMAGE_MAGIC_V1 && magic_byte != ESP_IMAGE_MAGIC_V2) {
#ifdef DEBUG
			os_printf("[OTA] ERROR: Invalid magic byte 0x%02X!\n", magic_byte);
#endif
			ota_cleanup_timers();
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
		ota_target_base_addr = conf.roms[ota_target_rom];
		
		// If resuming from a 4KB boundary, re-erase the boundary sector before writing
		current_flash_addr = ota_target_base_addr + ota_received_bytes;
		sector_to_erase = current_flash_addr / SPI_SECTOR_SIZE;
		spi_flash_erase_sector(sector_to_erase);

		// Initialize the rboot flash writing engine
		ota_status = rboot_write_init(current_flash_addr);
#ifdef DEBUG
		os_printf("[OTA] Flashing to ROM %d (0x%08X) offset %u...\n", ota_target_rom, ota_target_base_addr, ota_received_bytes);
#endif
		if (ota_received_bytes > 0) {
			ota_rehash_flash();
		}
	}

	if (is_valid_binary && len > 0) {
		if (ota_received_bytes + len > MAX_FIRMWARE_SIZE) {
#ifdef DEBUG
			os_printf("[OTA] ERROR: Firmware image exceeds partition limit!\n");
#endif
			ota_cleanup_timers();
			if (ota_mqtt_client) {
				mqtt_rpc_ota_status(ota_mqtt_client, "error_file_too_large");
			}
			is_valid_binary = false;
			ota_in_progress = false;
			espconn_abort(&ota_conn);
			return;
		}

		// Calculate SHA-256 hash stream
		if (has_sha256_check_1 || has_sha256_check_2) {
			sha256_update(&ota_sha_ctx, (uint8_t *)pdata, len);
		}

		if (!rboot_write_flash(&ota_status, (uint8_t *)pdata, len)) {
#ifdef DEBUG
			os_printf("[OTA] ERROR: Flash write failed at offset %u!\n", ota_received_bytes);
#endif
			ota_cleanup_timers();
			if (ota_mqtt_client) {
				mqtt_rpc_ota_status(ota_mqtt_client, "error_flash_write");
			}
			is_valid_binary = false;
			ota_in_progress = false;
			espconn_disconnect(&ota_conn);
		}
		else {
			ota_received_bytes += len;

			// --- START TEMPORARY RESUME TEST ---
			static bool forced_drop = false;
			if (!forced_drop && ota_received_bytes > 150000) { // Sabotage at ~150KB
#ifdef DEBUG
				os_printf("\n[OTA-TEST] Artificially killing socket to test chunked resume!\n\n");
#endif
				forced_drop = true;
				espconn_abort(&ota_conn);
				return;
			}
			// --- END TEMPORARY RESUME TEST ---

			// Send periodic MQTT progress reports every 10%
			if (ota_content_length > 0) {
				current_progress = (uint8_t)((ota_received_bytes * 100) / ota_content_length);

				if (current_progress >= last_reported_progress + 10 && current_progress < 100) {
					last_reported_progress = (current_progress / 10) * 10;
#ifdef DEBUG
					os_printf("[OTA] Progress: %u%%\n", last_reported_progress);
#endif
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
	os_timer_disarm(&ota_timeout_timer);
#ifdef DEBUG
	os_printf("[OTA] TCP Connection error (%d)\n", err);
#endif
	ota_trigger_reconnect();
}

ICACHE_FLASH_ATTR
static void ota_tcp_discon_cb(void *arg) {
	char status_msg[32];
	uint8_t calculated_sha256[32];
	bool matched;

	os_timer_disarm(&ota_timeout_timer);

	// Only finalize and reboot if the file was a valid binary and fully downloaded
	if (is_valid_binary && (ota_content_length == 0 || ota_received_bytes >= ota_content_length)) {
		ota_cleanup_timers();
		rboot_write_end(&ota_status);
		
		// 1. Enforce minimum received byte check to prevent soft-bricking on truncated downloads
		if (ota_received_bytes < MIN_FIRMWARE_SIZE) {
#ifdef DEBUG
			os_printf("[OTA] ERROR: Downloaded image too small (%u bytes)\n", ota_received_bytes);
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
			os_printf("[OTA] ERROR: Size mismatch. Expected %u, got %u\n", ota_content_length, ota_received_bytes);
#endif
			if (ota_mqtt_client) {
				mqtt_rpc_ota_status(ota_mqtt_client, "error_truncated");
			}
			ota_in_progress = false;
			return;
		}
		
		// 3. Verify Cryptographic Signature strictly against target ROM slot hash
		if (has_sha256_check_1 || has_sha256_check_2) {
			matched = false;
			sha256_final(&ota_sha_ctx, calculated_sha256);

			if (ota_target_rom == 0 && has_sha256_check_1) {
				if (os_memcmp(calculated_sha256, expected_sha256_1, 32) == 0) {
					matched = true;
				}
			}
			else if (ota_target_rom == 1 && has_sha256_check_2) {
				if (os_memcmp(calculated_sha256, expected_sha256_2, 32) == 0) {
					matched = true;
				}
			}

			if (!matched) {
#ifdef DEBUG
				os_printf("[OTA] ERROR: SHA-256 checksum mismatch for target ROM slot %d!\n", ota_target_rom);
#endif
				if (ota_mqtt_client) {
					mqtt_rpc_ota_status(ota_mqtt_client, "error_checksum_mismatch");
				}
				ota_in_progress = false;
				return;
			}
#ifdef DEBUG
			os_printf("[OTA] SUCCESS: SHA-256 verified for ROM slot %d.\n", ota_target_rom);
#endif
		}
		
		// 4. Stage a temporary boot to the target slot.
		// If the new binary crashes or fails to connect/pass self-test,
		// rBoot will automatically roll back to the old working slot on next reset.
		rboot_set_temp_rom(ota_target_rom);

#ifdef DEBUG
		os_printf("[OTA] Download complete. Staging temp boot to rom %d...\n", ota_target_rom);
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
		// Connection closed prematurely before completing image
#ifdef DEBUG
		os_printf("[OTA] Disconnected before valid download completed.\n");
#endif
		ota_trigger_reconnect();
	}
}

ICACHE_FLASH_ATTR
static void ota_tcp_connect_cb(void *arg) {
	char request[512];
	
	if (ota_received_bytes > 0) {
		// Request Range starting at 4KB sector boundary
		tfp_snprintf(request, sizeof(request),
			"GET %s HTTP/1.1\r\n"
			"Host: %s\r\n"
			"Range: bytes=%u-\r\n"
			"Connection: close\r\n"
			"User-Agent: ESP8266-OTA\r\n"
			"\r\n", ota_path, ota_host, ota_received_bytes);
	} else {
		// Format the HTTP GET request. Using HTTP/1.0 ensures the server
		// closes the connection automatically when the file finishes downloading.
		tfp_snprintf(request, sizeof(request),
			"GET %s HTTP/1.0\r\n"
			"Host: %s\r\n"
			"Connection: close\r\n"
			"User-Agent: ESP8266-OTA\r\n"
			"\r\n", ota_path, ota_host);
	}

	espconn_send(&ota_conn, (uint8_t *)request, strlen(request));
}

ICACHE_FLASH_ATTR
static void ota_dns_found_cb(const char *name, ip_addr_t *ipaddr, void *arg) {
	if (ipaddr == NULL) {
		os_timer_disarm(&ota_timeout_timer);
#ifdef DEBUG
		os_printf("[OTA] ERROR: DNS resolution failed!\n");
#endif
		ota_trigger_reconnect();
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
bool start_ota_update(MQTT_Client *client, const char *url, const char *hex_sha256_1, const char *hex_sha256_2, uint8_t *out_target_rom) {
	const char *p;
	const char *slash;
	const char *colon;
	const char *host_end;
	size_t host_len;
	static char ota_url[256];
	uint8_t current_rom;
	err_t err;

	ota_mqtt_client = client;

	ota_retry_count = 0;
	is_partial_content = false;
	ota_in_progress = true;

	if (hex_sha256_1 && strlen(hex_sha256_1) == 64) {
		hex_to_bytes(hex_sha256_1, expected_sha256_1, 32);
		has_sha256_check_1 = true;
	} else {
		has_sha256_check_1 = false;
	}

	if (hex_sha256_2 && strlen(hex_sha256_2) == 64) {
		hex_to_bytes(hex_sha256_2, expected_sha256_2, 32);
		has_sha256_check_2 = true;
	} else {
		has_sha256_check_2 = false;
	}

	if (has_sha256_check_1 || has_sha256_check_2) {
		sha256_init(&ota_sha_ctx);
	}

	if (!rboot_get_last_boot_rom(&current_rom)) {
		current_rom = rboot_get_current_rom();
	}
	
	ota_target_rom = (current_rom == 0) ? 1 : 0;
	
	if (out_target_rom) {
		*out_target_rom = ota_target_rom;
	}
	
	ota_content_length = 0;
	ota_received_bytes = 0;

	// Reject URLs that cannot fit in the parsing buffer instead of silently truncating them.
	if (!url || strlen(url) >= sizeof(ota_url)) {
#ifdef DEBUG
		os_printf("[OTA] ERROR: URL is missing or too long!\n");
#endif
		if (ota_mqtt_client) {
			mqtt_rpc_ota_status(ota_mqtt_client, "error_invalid_url");
		}
		ota_in_progress = false;
		return false;
	}
	strcpy(ota_url, url);

#ifdef DEBUG
	os_printf("[OTA] Request URL: %s\n", ota_url);
#endif

	// 1. Parse host, port, and full path (including query string).
	// The host and path must fit their destination buffers, including the NUL.
	p = ota_url;
	if (strncmp(p, "http://", 7) == 0) {
		p += 7;
	}

	slash = strchr(p, '/');
	colon = strchr(p, ':');

	if (slash) {
		if (strlen(slash) >= sizeof(ota_path)) {
#ifdef DEBUG
			os_printf("[OTA] ERROR: URL path is too long!\n");
#endif
			ota_cleanup_timers();
			if (ota_mqtt_client) {
				mqtt_rpc_ota_status(ota_mqtt_client, "error_invalid_url");
			}
			ota_in_progress = false;
			return false;
		}
		strcpy(ota_path, slash);
	}
	else {
		strcpy(ota_path, "/");
	}

	// Extract host and optional port, checking the host length before copying.
	if (colon && (!slash || colon < slash)) {
		if (colon == p || (size_t)(colon - p) >= sizeof(ota_host)) {
			ota_cleanup_timers();
			if (ota_mqtt_client) {
				mqtt_rpc_ota_status(ota_mqtt_client, "error_invalid_url");
			}
			ota_in_progress = false;
			return false;
		}
		memcpy(ota_host, p, colon - p);
		ota_host[colon - p] = '\0';
		ota_port = atoi(colon + 1);
		if (ota_port < 1 || ota_port > 65535) {
			ota_cleanup_timers();
			if (ota_mqtt_client) {
				mqtt_rpc_ota_status(ota_mqtt_client, "error_invalid_url");
			}
			ota_in_progress = false;
			return false;
		}
	}
	else {
		host_end = slash ? slash : p + strlen(p);
		host_len = host_end - p;
		if (host_len == 0 || host_len >= sizeof(ota_host)) {
			ota_cleanup_timers();
			if (ota_mqtt_client) {
				mqtt_rpc_ota_status(ota_mqtt_client, "error_invalid_url");
			}
			ota_in_progress = false;
			return false;
		}
		memcpy(ota_host, p, host_len);
		ota_host[host_len] = '\0';
		ota_port = OTA_DEFAULT_PORT;
	}

#ifdef DEBUG
	os_printf("[OTA] Parsed Host: %s | Port: %d | Path: %s\n", ota_host, ota_port, ota_path);
	os_printf("[OTA] HTTP GET Request:\nGET %s HTTP/1.0\r\nHost: %s\r\n\n", ota_path, ota_host);
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

	// Arm network inactivity timeout timer before initiating socket connection
	os_timer_disarm(&ota_timeout_timer);
	os_timer_setfn(&ota_timeout_timer, (os_timer_func_t *)ota_timeout_timer_cb, NULL);
	os_timer_arm(&ota_timeout_timer, OTA_NETWORK_TIMEOUT_MS, 0);

	// 3. Resolve DNS (or connect immediately if it's an IP)
	err = dns_gethostbyname(ota_host, &ota_ip, ota_dns_found_cb, &ota_conn);
	if (err == ERR_OK) {
		ota_dns_found_cb(ota_host, &ota_ip, &ota_conn);
	}

	return true;
}
