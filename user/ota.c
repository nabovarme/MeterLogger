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

#define OTA_REBOOT_DELAY_MS    6000
#define ESP_IMAGE_MAGIC_V1     0xE9
#define ESP_IMAGE_MAGIC_V2     0xEA
#define OTA_DEFAULT_PORT       80
#define OTA_MAX_HEADER_SIZE    1024
#define MIN_FIRMWARE_SIZE      102400 // 100 KB minimum sane size
#define MAX_FIRMWARE_SIZE      0x69000 // 420 KB max safe partition size
#define OTA_NETWORK_TIMEOUT_MS 15000   // 15s timeout

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

// Declare static buffers with explicit 4-byte alignment
static char _align_32_bit ota_header_buffer[OTA_MAX_HEADER_SIZE];
static uint16_t ota_header_len = 0;

// Remainder buffer to ensure we only ever pass multiples of 4 bytes to rboot
static uint8_t _align_32_bit ota_remainder[4];
static uint8_t ota_remainder_len = 0;

// Global string literals aligned to 4 bytes to prevent os_strstr unaligned flash reads
static const char _align_32_bit str_rnrn[]  = "\r\n\r\n";
static const char _align_32_bit str_200ok[] = "200 OK";
static const char _align_32_bit str_200[]   = "200";
static const char _align_32_bit str_cl_up[] = "Content-Length: ";
static const char _align_32_bit str_cl_lo[] = "content-length: ";
static const char _align_32_bit str_http_get_fmt[] = 
	"GET %s HTTP/1.0\r\n"
	"Host: %s\r\n"
	"Connection: close\r\n"
	"User-Agent: ESP8266-OTA\r\n"
	"\r\n";

static char ota_host[64];
static char ota_path[128];
static int ota_port = OTA_DEFAULT_PORT;

static os_timer_t ota_reboot_timer;
static os_timer_t ota_timeout_timer;

ICACHE_FLASH_ATTR
static void ota_reboot_timer_cb(void *arg) {
	system_restart_defered();
}

ICACHE_FLASH_ATTR
static void ota_timeout_timer_cb(void *arg) {
	if (ota_in_progress) {
#ifdef DEBUG
		os_printf("[OTA DBG] Timeout timer triggered! Aborting.\n");
#endif
		if (ota_mqtt_client) {
			mqtt_rpc_ota_status(ota_mqtt_client, "error_timeout");
		}
		espconn_abort(&ota_conn);
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
	uint32_t target_addr;
	char *cl;
	static uint8_t last_reported_progress = 0;
	uint8_t current_progress = 0;
	char progress_msg[32];
	uint16_t copy_len;
	uint16_t total_header_size;
	uint16_t previous_header_len;
	uint16_t body_offset;
	uint8_t *chunk_ptr;
	uint16_t chunk_len;
	uint16_t aligned_len;
	uint8_t new_remainder_len;
	uint8_t fill;
	uint8_t *write_ptr;
	bool is_write_ok;

	// 4-byte aligned write buffer on stack
	uint32_t _align_32_bit aligned_chunk[1460 / 4 + 1];

#ifdef DEBUG
	os_printf("[OTA DBG] recv_cb START: rx_ptr=0x%08X (aligned=%d), len=%u\n", 
		(uint32_t)pusrdata, is_aligned(pusrdata, 4), length);
#endif

	// Refresh timeout timer safely
	os_timer_disarm(&ota_timeout_timer);
	os_timer_arm(&ota_timeout_timer, OTA_NETWORK_TIMEOUT_MS, 0);

	pdata = pusrdata;
	len = length;

	if (!headers_parsed) {
		previous_header_len = ota_header_len;

		copy_len = length;
		if (ota_header_len + copy_len > OTA_MAX_HEADER_SIZE - 1) {
			copy_len = OTA_MAX_HEADER_SIZE - 1 - ota_header_len;
		}
		
#ifdef DEBUG
		os_printf("[OTA DBG] Copying header chunk: offset=%u, len=%u\n", ota_header_len, copy_len);
#endif
		os_memcpy(ota_header_buffer + ota_header_len, pusrdata, copy_len);
		ota_header_len += copy_len;
		ota_header_buffer[ota_header_len] = '\0';
		
#ifdef DEBUG
		os_printf("[OTA DBG] Searching str_rnrn in ota_header_buffer (total_hdr_len=%u)\n", ota_header_len);
#endif
		body_marker = (char *)os_strstr(ota_header_buffer, str_rnrn);
		if (body_marker) {
#ifdef DEBUG
			os_printf("[OTA DBG] Body marker found at ptr=0x%08X\n", (uint32_t)body_marker);
			os_printf("[OTA DBG] Searching str_200ok\n");
#endif
			if (os_strstr(ota_header_buffer, str_200ok) == NULL && os_strstr(ota_header_buffer, str_200) == NULL) {
#ifdef DEBUG
				os_printf("[OTA DBG] ERROR: Non-200 HTTP response!\n");
#endif
				if (ota_mqtt_client) {
					mqtt_rpc_ota_status(ota_mqtt_client, "error_http_not_200");
				}
				ota_in_progress = false;
				espconn_disconnect(&ota_conn);
				return;
			}

#ifdef DEBUG
			os_printf("[OTA DBG] Searching str_cl_up\n");
#endif
			cl = (char *)os_strstr(ota_header_buffer, str_cl_up);
			if (!cl) {
#ifdef DEBUG
				os_printf("[OTA DBG] Searching str_cl_lo\n");
#endif
				cl = (char *)os_strstr(ota_header_buffer, str_cl_lo);
			}
			if (cl) {
				ota_content_length = atoi(cl + 16);
#ifdef DEBUG
				os_printf("[OTA DBG] Content-Length parsed: %u\n", ota_content_length);
#endif
			}

			headers_parsed = true;
			last_reported_progress = 0;
			
			// Calculate exact body slicing offset for current packet
			total_header_size = (body_marker + 4) - ota_header_buffer;
#ifdef DEBUG
			os_printf("[OTA DBG] total_hdr_size=%u, prev_hdr_len=%u\n", total_header_size, previous_header_len);
#endif

			if (total_header_size > previous_header_len) {
				body_offset = total_header_size - previous_header_len;
#ifdef DEBUG
				os_printf("[OTA DBG] Slicing body offset=%u\n", body_offset);
#endif
				if (body_offset <= length) {
					pdata = pusrdata + body_offset;
					len = length - body_offset;
				} else {
					len = 0;
				}
			} else {
				len = 0;
			}
#ifdef DEBUG
			os_printf("[OTA DBG] Sliced payload: pdata=0x%08X (aligned=%d), len=%u\n", 
				(uint32_t)pdata, is_aligned(pdata, 4), len);
#endif
		} else {
#ifdef DEBUG
			os_printf("[OTA DBG] Header incomplete, waiting for next packet...\n");
#endif
			if (ota_header_len >= OTA_MAX_HEADER_SIZE - 1) {
#ifdef DEBUG
				os_printf("[OTA DBG] ERROR: Header overflow!\n");
#endif
				if (ota_mqtt_client) {
					mqtt_rpc_ota_status(ota_mqtt_client, "error_header_overflow");
				}
				ota_in_progress = false;
				espconn_disconnect(&ota_conn);
			}
			return;
		}
	}

	// Validate magic byte on the very first binary chunk
	if (headers_parsed && !is_valid_binary && len > 0) {
		magic_byte = (uint8_t)pdata[0];
#ifdef DEBUG
		os_printf("[OTA DBG] Checking magic byte at pdata[0]=0x%02X\n", magic_byte);
#endif
		
		if (magic_byte != ESP_IMAGE_MAGIC_V1 && magic_byte != ESP_IMAGE_MAGIC_V2) {
#ifdef DEBUG
			os_printf("[OTA DBG] ERROR: Invalid magic byte 0x%02X!\n", magic_byte);
#endif
			if (ota_mqtt_client) {
				mqtt_rpc_ota_status(ota_mqtt_client, "error_invalid_magic");
			}
			ota_in_progress = false;
			espconn_disconnect(&ota_conn);
			return;
		}

		is_valid_binary = true;

		conf = rboot_get_config();
		target_addr = conf.roms[ota_target_rom];
#ifdef DEBUG
		os_printf("[OTA DBG] Calling rboot_write_init(target_addr=0x%08X, target_rom=%d)\n", 
			target_addr, ota_target_rom);
#endif

		ota_status = rboot_write_init(target_addr);
#ifdef DEBUG
		os_printf("[OTA DBG] rboot_write_init complete: start_addr=0x%08X, start_sector=%u\n", 
			ota_status.start_addr, ota_status.start_sector);
#endif
	}

	if (is_valid_binary && len > 0) {
		chunk_ptr = (uint8_t *)pdata;
		chunk_len = len;

#ifdef DEBUG
		os_printf("[OTA DBG] Preparing flash write: rx_bytes=%u, chunk_len=%u\n", ota_received_bytes, chunk_len);
#endif

		if (ota_received_bytes + chunk_len > MAX_FIRMWARE_SIZE) {
#ifdef DEBUG
			os_printf("[OTA DBG] ERROR: Firmware image exceeds limit (%u > %u)!\n", 
				ota_received_bytes + chunk_len, (uint32_t)MAX_FIRMWARE_SIZE);
#endif
			if (ota_mqtt_client) {
				mqtt_rpc_ota_status(ota_mqtt_client, "error_file_too_large");
			}
			is_valid_binary = false;
			ota_in_progress = false;
			espconn_abort(&ota_conn);
			return;
		}

		system_soft_wdt_feed();

		// 1. Process remainder from previous packet first to reach 4-byte boundary
		if (ota_remainder_len > 0) {
			fill = 4 - ota_remainder_len;
#ifdef DEBUG
			os_printf("[OTA DBG] Filling remainder: rem_len=%u, fill=%u\n", ota_remainder_len, fill);
#endif
			if (chunk_len >= fill) {
				os_memcpy(ota_remainder + ota_remainder_len, chunk_ptr, fill);
#ifdef DEBUG
				os_printf("[OTA DBG] Writing remainder chunk to flash: flash_offset=0x%08X, len=4\n", 
					ota_status.start_addr);
#endif
				if (!rboot_write_flash(&ota_status, ota_remainder, 4)) {
#ifdef DEBUG
					os_printf("[OTA DBG] ERROR: rboot_write_flash remainder failed!\n");
#endif
					if (ota_mqtt_client) {
						mqtt_rpc_ota_status(ota_mqtt_client, "error_flash_write");
					}
					is_valid_binary = false;
					ota_in_progress = false;
					espconn_disconnect(&ota_conn);
					return;
				}
				ota_received_bytes += fill;
				chunk_ptr += fill;
				chunk_len -= fill;
				ota_remainder_len = 0;
			} else {
				os_memcpy(ota_remainder + ota_remainder_len, chunk_ptr, chunk_len);
				ota_remainder_len += chunk_len;
				ota_received_bytes += chunk_len;
				chunk_len = 0;
			}
		}

		// 2. Write only 4-byte aligned blocks; store remaining 1-3 bytes for next packet
		if (chunk_len > 0) {
			aligned_len = chunk_len & ~3; // Round down to nearest multiple of 4
			new_remainder_len = chunk_len & 3;

			if (aligned_len > 0) {
				write_ptr = chunk_ptr;
				if (!is_aligned(chunk_ptr, 4)) {
#ifdef DEBUG
					os_printf("[OTA DBG] chunk_ptr 0x%08X unaligned! Copying to aligned_chunk 0x%08X\n", 
						(uint32_t)chunk_ptr, (uint32_t)aligned_chunk);
#endif
					os_memcpy(aligned_chunk, chunk_ptr, aligned_len);
					write_ptr = (uint8_t *)aligned_chunk;
				}

#ifdef DEBUG
				os_printf("[OTA DBG] Executing rboot_write_flash: flash_offset=0x%08X, write_ptr=0x%08X (aligned=%d), len=%u\n", 
					ota_status.start_addr, (uint32_t)write_ptr, is_aligned(write_ptr, 4), aligned_len);
#endif

				is_write_ok = rboot_write_flash(&ota_status, write_ptr, aligned_len);
#ifdef DEBUG
				os_printf("[OTA DBG] rboot_write_flash result=%d\n", is_write_ok);
#endif

				if (!is_write_ok) {
#ifdef DEBUG
					os_printf("[OTA DBG] ERROR: rboot_write_flash returned false!\n");
#endif
					if (ota_mqtt_client) {
						mqtt_rpc_ota_status(ota_mqtt_client, "error_flash_write");
					}
					is_valid_binary = false;
					ota_in_progress = false;
					espconn_disconnect(&ota_conn);
					return;
				}
				ota_received_bytes += aligned_len;
				chunk_ptr += aligned_len;
			}

			if (new_remainder_len > 0) {
#ifdef DEBUG
				os_printf("[OTA DBG] Saving new remainder: len=%u\n", new_remainder_len);
#endif
				os_memcpy(ota_remainder, chunk_ptr, new_remainder_len);
				ota_remainder_len = new_remainder_len;
				ota_received_bytes += new_remainder_len;
			}
		}

		// Send periodic MQTT progress reports every 10%
		if (ota_content_length > 0) {
			current_progress = (uint8_t)((ota_received_bytes * 100) / ota_content_length);

			if (current_progress >= last_reported_progress + 10 && current_progress < 100) {
				last_reported_progress = (current_progress / 10) * 10;
#ifdef DEBUG
				os_printf("[OTA DBG] Progress: %u%%\n", last_reported_progress);
#endif
				if (ota_mqtt_client) {
					tfp_snprintf(progress_msg, sizeof(progress_msg), "flashing_%d%%", last_reported_progress);
					mqtt_rpc_ota_status(ota_mqtt_client, progress_msg);
				}
			}
		}
	}

#ifdef DEBUG
	os_printf("[OTA DBG] recv_cb END\n");
#endif
}

ICACHE_FLASH_ATTR
static void ota_tcp_recon_cb(void *arg, sint8 err) {
	os_timer_disarm(&ota_timeout_timer); 
#ifdef DEBUG
	os_printf("[OTA DBG] recon_cb: err=%d\n", err);
#endif
	if (ota_mqtt_client) {
		mqtt_rpc_ota_status(ota_mqtt_client, "error_tcp_drop");
	}
	ota_in_progress = false;
}

ICACHE_FLASH_ATTR
static void ota_tcp_discon_cb(void *arg) {
	char status_msg[32];

	os_timer_disarm(&ota_timeout_timer); 
#ifdef DEBUG
	os_printf("[OTA DBG] discon_cb: total_rx=%u, valid_bin=%d\n", ota_received_bytes, is_valid_binary);
#endif

	if (is_valid_binary) {
		// Flush any final remainder bytes by padding with 0xFF to reach 4-byte multiple
		if (ota_remainder_len > 0) {
#ifdef DEBUG
			os_printf("[OTA DBG] Flushing final remainder: len=%u\n", ota_remainder_len);
#endif
			while (ota_remainder_len < 4) {
				ota_remainder[ota_remainder_len++] = 0xFF;
			}
			rboot_write_flash(&ota_status, ota_remainder, 4);
		}

		if (ota_received_bytes < MIN_FIRMWARE_SIZE) {
#ifdef DEBUG
			os_printf("[OTA DBG] ERROR: Image too small (%u < %u)\n", 
				ota_received_bytes, (uint32_t)MIN_FIRMWARE_SIZE);
#endif
			if (ota_mqtt_client) {
				mqtt_rpc_ota_status(ota_mqtt_client, "error_too_small");
			}
			ota_in_progress = false;
			return;
		}

		if (ota_content_length > 0 && ota_received_bytes != ota_content_length) {
#ifdef DEBUG
			os_printf("[OTA DBG] ERROR: Size mismatch (%u != %u)\n", 
				ota_received_bytes, ota_content_length);
#endif
			if (ota_mqtt_client) {
				mqtt_rpc_ota_status(ota_mqtt_client, "error_truncated");
			}
			ota_in_progress = false;
			return;
		}
		
#ifdef DEBUG
		os_printf("[OTA DBG] Calling rboot_write_end...\n");
#endif
		rboot_write_end(&ota_status);

#ifdef DEBUG
		os_printf("[OTA DBG] Staging temp rom %d...\n", ota_target_rom);
#endif
		rboot_set_temp_rom(ota_target_rom);

		if (ota_mqtt_client) {
			tfp_snprintf(status_msg, sizeof(status_msg), "success&target_rom=%d", ota_target_rom + 1);
			mqtt_rpc_ota_status(ota_mqtt_client, status_msg);
			
			tfp_snprintf(status_msg, sizeof(status_msg), "restarting_in_%ds", (OTA_REBOOT_DELAY_MS + SYSTEM_RESTART_DELAY_MS) / 1000);
			mqtt_rpc_ota_status(ota_mqtt_client, status_msg);
		}
		
		os_timer_disarm(&ota_reboot_timer);
		os_timer_setfn(&ota_reboot_timer, (os_timer_func_t *)ota_reboot_timer_cb, NULL);
		os_timer_arm(&ota_reboot_timer, OTA_REBOOT_DELAY_MS, 0);
	}
	else {
#ifdef DEBUG
		os_printf("[OTA DBG] Disconnected before valid download completed.\n");
#endif
		if (ota_mqtt_client) {
			mqtt_rpc_ota_status(ota_mqtt_client, "error_disconnected");
		}
		ota_in_progress = false; 
	}
}

ICACHE_FLASH_ATTR
static void ota_tcp_connect_cb(void *arg) {
	char request[512];
	
#ifdef DEBUG
	os_printf("[OTA DBG] connect_cb: str_http_get_fmt=0x%08X (aligned=%d)\n", 
		(uint32_t)str_http_get_fmt, is_aligned(str_http_get_fmt, 4));
#endif

	tfp_snprintf(request, sizeof(request), str_http_get_fmt, ota_path, ota_host);
	
#ifdef DEBUG
	os_printf("[OTA DBG] Sending request: %s\n", request);
#endif
	espconn_send(&ota_conn, (uint8_t *)request, strlen(request));
}

ICACHE_FLASH_ATTR
static void ota_dns_found_cb(const char *name, ip_addr_t *ipaddr, void *arg) {
	if (ipaddr == NULL) {
		os_timer_disarm(&ota_timeout_timer);
#ifdef DEBUG
		os_printf("[OTA DBG] DNS failed!\n");
#endif
		if (ota_mqtt_client) {
			mqtt_rpc_ota_status(ota_mqtt_client, "error_dns");
		}
		ota_in_progress = false; 
		return;
	}

#ifdef DEBUG
	os_printf("[OTA DBG] DNS resolved: %d.%d.%d.%d\n",
		*((uint8 *)&ipaddr->addr), *((uint8 *)&ipaddr->addr + 1),
		*((uint8 *)&ipaddr->addr + 2), *((uint8 *)&ipaddr->addr + 3));
#endif

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
	ota_remainder_len = 0;

	strncpy(ota_url, url, sizeof(ota_url) - 1);
	ota_url[sizeof(ota_url) - 1] = '\0';

#ifdef DEBUG
	os_printf("[OTA DBG] start_ota_upgrade: url=%s, current_rom=%d, target_rom=%d\n", 
		ota_url, current_rom, ota_target_rom);
#endif

	p = ota_url;
	if (strncmp(p, "http://", 7) == 0) {
		p += 7;
	}

	slash = strchr(p, '/');
	colon = strchr(p, ':');

	if (slash) {
		strncpy(ota_path, slash, sizeof(ota_path) - 1);
	} else {
		strcpy(ota_path, "/");
	}

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
	os_printf("[OTA DBG] Parsed host=%s, port=%d, path=%s\n", ota_host, ota_port, ota_path);
#endif

	headers_parsed = false;
	is_valid_binary = false;
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

	return true;
}
