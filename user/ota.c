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

static struct espconn ota_conn;
static esp_tcp ota_tcp;
static ip_addr_t ota_ip;
static rboot_write_status ota_status;
static uint8_t ota_target_rom;
static bool headers_parsed = false;
static bool is_valid_binary = false;

static char ota_host[64];
static char ota_path[128];
static int ota_port = 80;

ICACHE_FLASH_ATTR 
static void ota_tcp_recv_cb(void *arg, char *pusrdata, unsigned short length) {
	char *pdata = pusrdata;
	uint16_t len = length;

	if (!headers_parsed) {
		// Check for 200 OK status code first
		if (os_strstr(pdata, "HTTP/1.1 200 OK") == NULL && os_strstr(pdata, "HTTP/1.0 200 OK") == NULL) {
#ifdef DEBUG
			os_printf("OTA Error: Non-200 HTTP response received!\n");
#endif
			espconn_disconnect(&ota_conn);
			return;
		}

		// Find the end of the HTTP headers
		char *body = (char *)os_strstr(pdata, "\r\n\r\n");
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
		uint8_t magic_byte = (uint8_t)pdata[0];
		
		// 0xE9 is the standard ESP8266 image magic byte; 0xEA is the v2 header
		if (magic_byte != 0xE9 && magic_byte != 0xEA) {
#ifdef DEBUG
			os_printf("OTA Error: Invalid magic byte (0x%02X). Expected 0xE9!\n", magic_byte);
			if (magic_byte == '<') { // 0x3C
				os_printf("OTA Error: Received HTML instead of firmware binary.\n");
			}
#endif
			espconn_disconnect(&ota_conn);
			return;
		}

		// Valid binary detected! Safe to initialize flash erasure.
		is_valid_binary = true;

		// Look up the exact flash memory address for the target rom slot
		rboot_config conf = rboot_get_config();
		uint32_t target_addr = conf.roms[ota_target_rom];
		
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
			espconn_disconnect(&ota_conn);
		}
	}
}

ICACHE_FLASH_ATTR 
static void ota_tcp_discon_cb(void *arg) {
	// Only finalize and reboot if the file was a valid binary
	if (is_valid_binary) {
		rboot_write_end(&ota_status);
		rboot_set_current_rom(ota_target_rom);
#ifdef DEBUG
		os_printf("OTA: Download complete. Rebooting to rom %d...\n", ota_target_rom);
#endif
		system_restart_defered();
	} else {
#ifdef DEBUG
		os_printf("OTA: Disconnected before valid download completed.\n");
#endif
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
		return;
	}

	os_memcpy(ota_conn.proto.tcp->remote_ip, &ipaddr->addr, 4);
	ota_conn.proto.tcp->remote_port = ota_port;
	ota_conn.proto.tcp->local_port = espconn_port();

	espconn_regist_connectcb(&ota_conn, ota_tcp_connect_cb);
	espconn_regist_recvcb(&ota_conn, ota_tcp_recv_cb);
	espconn_regist_disconcb(&ota_conn, ota_tcp_discon_cb);

	espconn_connect(&ota_conn);
}

ICACHE_FLASH_ATTR 
bool start_ota_upgrade(const char *url, uint8_t *out_target_rom) {
	const char *p;
	const char *slash;
	const char *colon;
	char ota_url[256];

	uint8_t current_rom = rboot_get_current_rom();
	ota_target_rom = (current_rom == 0) ? 1 : 0;
	
	if (out_target_rom) {
		*out_target_rom = ota_target_rom;
	}

	// Format full URL by appending the target binary name (e.g. user2.bin)
	tfp_snprintf(ota_url, sizeof(ota_url), "%suser%d.bin", url, ota_target_rom + 1);

	// 1. Parse the URL into Host, Port, and Path
	p = ota_url;
	if (strncmp(p, "http://", 7) == 0) p += 7;

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

	// 2. Setup the TCP connection
	headers_parsed = false;
	is_valid_binary = false;
	memset(&ota_conn, 0, sizeof(ota_conn));
	memset(&ota_tcp, 0, sizeof(ota_tcp));
	ota_conn.type = ESPCONN_TCP;
	ota_conn.state = ESPCONN_NONE;
	ota_conn.proto.tcp = &ota_tcp;

	// 3. Resolve DNS (or connect immediately if it's an IP)
	err_t err = dns_gethostbyname(ota_host, &ota_ip, ota_dns_found_cb, &ota_conn);
	if (err == ERR_OK) {
		ota_dns_found_cb(ota_host, &ota_ip, &ota_conn);
	}

	return true;
}
