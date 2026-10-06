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

static char ota_host[64];
static char ota_path[128];
static int ota_port = 80;

ICACHE_FLASH_ATTR 
static void ota_tcp_recv_cb(void *arg, char *pusrdata, unsigned short length) {
	char *pdata = pusrdata;
	uint16_t len = length;

	if (!headers_parsed) {
		// Find the end of the HTTP headers
		char *body = (char *)os_strstr(pdata, "\r\n\r\n");
		if (body) {
			headers_parsed = true;
			body += 4; // Skip past the \r\n\r\n
			len -= (body - pdata);
			pdata = body;

			// Look up the exact flash memory address for the target rom slot
			rboot_config conf = rboot_get_config();
			uint32_t target_addr = conf.roms[ota_target_rom];
			
			// Initialize the rboot flash writing engine
			ota_status = rboot_write_init(target_addr);
		} else {
			// Still waiting for the end of headers
			return;
		}
	}

	// Write incoming payload chunks directly to flash memory
	if (len > 0) {
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
	// When the HTTP/1.0 server closes the connection, the file is fully downloaded
	if (headers_parsed) {
		rboot_write_end(&ota_status);
		rboot_set_current_rom(ota_target_rom);
#ifdef DEBUG
		os_printf("OTA: Download complete. Rebooting to rom %d...\n", ota_target_rom);
#endif
		system_restart_defered();
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
