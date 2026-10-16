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
#define OTA_PROGRESS_INTERVAL_MS 10000 // Report OTA progress every 10 seconds
#define ESP_IMAGE_MAGIC_V1     0xE9
#define ESP_IMAGE_MAGIC_V2     0xEA
#define OTA_DEFAULT_PORT       80
#define OTA_MAX_HEADER_SIZE    1024
#define MIN_FIRMWARE_SIZE      102400  // 100 KB minimum sane size
#define MAX_FIRMWARE_SIZE      0x69000 // Maximum safe firmware size
#define OTA_NETWORK_TIMEOUT_MS 15000   // 15s inactivity timeout

// Bring in the lock from mqtt_rpc.c so we can unlock on failure
extern bool ota_in_progress;

typedef enum {
	OTA_RETRY_FAILURE_OTHER,
	OTA_RETRY_FAILURE_DNS,
	OTA_RETRY_FAILURE_TCP,
	OTA_RETRY_FAILURE_INCOMPLETE_HTTP_RESPONSE
} ota_retry_reason_t;

static MQTT_Client *ota_mqtt_client = NULL;

static struct espconn ota_conn;
static esp_tcp ota_tcp;
static ip_addr_t ota_ip;
static rboot_write_status ota_status;
static uint8_t ota_target_rom;
static uint32_t ota_target_base_addr = 0;

static bool headers_parsed = false;
static bool is_valid_binary = false;

static uint32_t ota_firmware_size = 0;
static bool ota_firmware_size_known = false;

static uint32_t ota_response_length = 0;
static uint32_t ota_response_received_bytes = 0;
static bool ota_response_length_known = false;
static int ota_response_status_code = 0;
static uint32_t ota_response_range_end = 0;
static uint32_t ota_response_range_total = 0;
static bool ota_response_range_valid = false;

static uint32_t ota_received_bytes = 0;
static uint32_t ota_request_offset = 0;

static uint8_t ota_retry_count = 0;
static bool ota_retry_pending = false;
static bool ota_continue_pending = false;
static bool ota_dns_pending = false;
static bool ota_incomplete_http_response_seen = false;
static bool ota_dns_failure_seen = false;
static bool ota_tcp_failure_seen = false;
static bool ota_other_failure_seen = false;

static char ota_header_buffer[OTA_MAX_HEADER_SIZE];
static uint16_t ota_header_len = 0;

static char ota_host[64];
static char ota_host_header[72];
static char ota_path[128];
static int ota_port = OTA_DEFAULT_PORT;

static os_timer_t ota_reboot_timer;
static os_timer_t ota_timeout_timer;
static os_timer_t ota_retry_timer;
static os_timer_t ota_progress_timer;

// SHA-256 state
static sha256_ctx_t ota_sha_ctx;
static uint8_t expected_sha256_1[32];
static uint8_t expected_sha256_2[32];
static bool has_sha256_check_1 = false;
static bool has_sha256_check_2 = false;

static void ota_trigger_reconnect(ota_retry_reason_t reason);
static void ota_dns_found_cb(const char *name, ip_addr_t *ipaddr, void *arg);
static bool ota_incomplete_http_response(void);

/*
 * Compare ASCII text without depending on a locale-aware library.
 */
ICACHE_FLASH_ATTR
static char ota_ascii_lower(char c) {
	if (c >= 'A' && c <= 'Z') {
		return c + ('a' - 'A');
	}

	return c;
}

ICACHE_FLASH_ATTR
static bool ota_ascii_equal_n(const char *a, const char *b, size_t len) {
	size_t i;

	for (i = 0; i < len; i++) {
		if (ota_ascii_lower(a[i]) != ota_ascii_lower(b[i])) {
			return false;
		}
	}

	return true;
}

/*
 * Find an HTTP header by name.
 *
 * Header names are case-insensitive. Duplicate occurrences are reported so
 * the caller can reject ambiguous Content-Length or Content-Range headers.
 */
ICACHE_FLASH_ATTR
static const char *ota_get_header_value(
	const char *headers,
	const char *name,
	bool *duplicate
) {
	const char *line;
	const char *line_end;
	const char *colon;
	const char *value = NULL;
	size_t name_len;

	*duplicate = false;
	name_len = strlen(name);

	line = strstr(headers, "\r\n");
	if (!line) {
		return NULL;
	}

	line += 2;

	while (line[0] && !(line[0] == '\r' && line[1] == '\n')) {
		line_end = strstr(line, "\r\n");
		if (!line_end) {
			return NULL;
		}

		colon = strchr(line, ':');

		if (colon && colon < line_end &&
			(size_t)(colon - line) == name_len &&
			ota_ascii_equal_n(line, name, name_len)) {

			if (value) {
				*duplicate = true;
			} else {
				value = colon + 1;

				while (*value == ' ' || *value == '\t') {
					value++;
				}
			}
		}

		line = line_end + 2;
	}

	return value;
}

/*
 * Parse an unsigned decimal integer and require the rest of the value to
 * contain only optional whitespace and the end of the header line.
 */
ICACHE_FLASH_ATTR
static bool ota_parse_uint32(const char *p, uint32_t *result) {
	uint32_t value = 0;
	bool have_digit = false;

	while (*p == ' ' || *p == '\t') {
		p++;
	}

	while (*p >= '0' && *p <= '9') {
		uint32_t digit = (uint32_t)(*p - '0');

		if (value > (0xFFFFFFFFUL - digit) / 10UL) {
			return false;
		}

		value = value * 10UL + digit;
		have_digit = true;
		p++;
	}

	if (!have_digit) {
		return false;
	}

	while (*p == ' ' || *p == '\t') {
		p++;
	}

	if (*p != '\r' && *p != '\n' && *p != '\0') {
		return false;
	}

	*result = value;
	return true;
}

/*
 * Parse Content-Range in the form:
 *
 *     bytes START-END/TOTAL
 *
 * The response must use a concrete total; wildcard totals are not accepted.
 */
ICACHE_FLASH_ATTR
static bool ota_parse_content_range(
	const char *p,
	uint32_t *range_start,
	uint32_t *range_end,
	uint32_t *total_size
) {
	uint32_t start = 0;
	uint32_t end = 0;
	uint32_t total = 0;
	bool have_digit;

	while (*p == ' ' || *p == '\t') {
		p++;
	}

	if (!ota_ascii_equal_n(p, "bytes", 5)) {
		return false;
	}

	p += 5;

	if (*p != ' ' && *p != '\t') {
		return false;
	}

	while (*p == ' ' || *p == '\t') {
		p++;
	}

	have_digit = false;
	while (*p >= '0' && *p <= '9') {
		uint32_t digit = (uint32_t)(*p - '0');

		if (start > (0xFFFFFFFFUL - digit) / 10UL) {
			return false;
		}

		start = start * 10UL + digit;
		have_digit = true;
		p++;
	}

	if (!have_digit || *p != '-') {
		return false;
	}

	p++;

	have_digit = false;
	while (*p >= '0' && *p <= '9') {
		uint32_t digit = (uint32_t)(*p - '0');

		if (end > (0xFFFFFFFFUL - digit) / 10UL) {
			return false;
		}

		end = end * 10UL + digit;
		have_digit = true;
		p++;
	}

	if (!have_digit || *p != '/') {
		return false;
	}

	p++;

	have_digit = false;
	while (*p >= '0' && *p <= '9') {
		uint32_t digit = (uint32_t)(*p - '0');

		if (total > (0xFFFFFFFFUL - digit) / 10UL) {
			return false;
		}

		total = total * 10UL + digit;
		have_digit = true;
		p++;
	}

	if (!have_digit) {
		return false;
	}

	while (*p == ' ' || *p == '\t') {
		p++;
	}

	if (*p != '\r' && *p != '\n' && *p != '\0') {
		return false;
	}

	*range_start = start;
	*range_end = end;
	*total_size = total;

	return true;
}

/*
 * Parse the HTTP status line. Only HTTP/1.0 and HTTP/1.1 are accepted.
 */
ICACHE_FLASH_ATTR
static bool ota_parse_status_code(int *status_code) {
	const char *line_end;
	const char *line;
	int code;

	line = ota_header_buffer;
	line_end = strstr(line, "\r\n");

	if (!line_end) {
		return false;
	}

	if (strncmp(line, "HTTP/1.0 ", 9) != 0 &&
		strncmp(line, "HTTP/1.1 ", 9) != 0) {
		return false;
	}

	if (line_end - line < 12) {
		return false;
	}

	if (line[9] < '0' || line[9] > '9' ||
		line[10] < '0' || line[10] > '9' ||
		line[11] < '0' || line[11] > '9') {
		return false;
	}

	code = (line[9] - '0') * 100 +
		(line[10] - '0') * 10 +
		(line[11] - '0');

	if (line[12] != ' ' && line[12] != '\r') {
		return false;
	}

	*status_code = code;
	return true;
}

/*
 * Parse an optional SHA-256 hex string. A missing/empty value disables this
 * digest; a non-empty malformed value is rejected instead of disabling the
 * integrity check silently.
 */
ICACHE_FLASH_ATTR
static bool ota_parse_sha256(
	const char *hex,
	uint8_t digest[32],
	bool *enabled
) {
	size_t i;

	*enabled = false;

	if (!hex || !*hex) {
		return true;
	}

	if (strlen(hex) != 64) {
		return false;
	}

	for (i = 0; i < 32; i++) {
		char high_char;
		char low_char;
		int high;
		int low;

		high_char = hex[i * 2];
		low_char = hex[i * 2 + 1];

		high = hex_char_to_val(high_char);
		low = hex_char_to_val(low_char);

		if (high < 0 || low < 0) {
			return false;
		}

		digest[i] = (uint8_t)((high << 4) | low);
	}

	*enabled = true;
	return true;
}

/*
 * Report the current download progress at a steady interval.
 *
 * The firmware size must be known before a meaningful percentage can be
 * calculated. A 100% flashing status is omitted because successful completion
 * is reported separately after finalizing the flash and verifying the image.
 */
ICACHE_FLASH_ATTR
static void ota_progress_timer_cb(void *arg) {
	char progress_msg[32];
	uint8_t current_progress;

	if (!ota_in_progress || !ota_firmware_size_known ||
		ota_firmware_size == 0) {
		return;
	}

	current_progress = (uint8_t)(
		(ota_received_bytes * 100UL) / ota_firmware_size
	);

	if (current_progress >= 100) {
		return;
	}

#ifdef DEBUG
	os_printf("[OTA] Progress: %u%%\n", current_progress);
#endif

	if (ota_mqtt_client) {
		tfp_snprintf(progress_msg, sizeof(progress_msg),
			"flashing_%d%%", current_progress);
		mqtt_rpc_ota_status(ota_mqtt_client, progress_msg);
	}
}

ICACHE_FLASH_ATTR
static void ota_cleanup_timers(void) {
	os_timer_disarm(&ota_timeout_timer);
	os_timer_disarm(&ota_retry_timer);
	os_timer_disarm(&ota_progress_timer);
}

ICACHE_FLASH_ATTR
static void ota_reboot_timer_cb(void *arg) {
	system_restart_defered();
}

/*
 * Stop the current OTA operation before aborting the connection. This prevents
 * connection callbacks from scheduling another retry after a fatal error.
 */
ICACHE_FLASH_ATTR
static void ota_fail_update(const char *status, bool abort_connection) {
	ota_cleanup_timers();
	os_timer_disarm(&ota_reboot_timer);

	ota_retry_pending = false;
	ota_continue_pending = false;
	ota_dns_pending = false;
	is_valid_binary = false;
	ota_in_progress = false;

	if (ota_mqtt_client) {
		mqtt_rpc_ota_status(ota_mqtt_client, (char *)status);
	}

	if (abort_connection) {
		espconn_abort(&ota_conn);
	}
}

/*
 * The inactivity timeout must schedule recovery itself. Do not rely on
 * espconn_abort() producing a callback: DNS lookup and connection setup can
 * time out before an established TCP connection exists.
 */
ICACHE_FLASH_ATTR
static void ota_timeout_timer_cb(void *arg) {
	bool was_waiting_for_dns;
	bool incomplete_http_response;

	if (!ota_in_progress || ota_retry_pending) {
		return;
	}

	was_waiting_for_dns = ota_dns_pending;
	incomplete_http_response = !was_waiting_for_dns &&
		ota_incomplete_http_response();
	ota_dns_pending = false;
	os_timer_disarm(&ota_timeout_timer);

#ifdef DEBUG
	os_printf("[OTA] Error: Network operation timed out! Scheduling recovery...\n");
#endif

	if (incomplete_http_response) {
		ota_tcp_failure_seen = true;
		ota_trigger_reconnect(OTA_RETRY_FAILURE_INCOMPLETE_HTTP_RESPONSE);
	} else if (was_waiting_for_dns) {
		ota_trigger_reconnect(OTA_RETRY_FAILURE_DNS);
	} else {
		ota_trigger_reconnect(OTA_RETRY_FAILURE_TCP);
	}

	if (!was_waiting_for_dns) {
		espconn_abort(&ota_conn);
	}
}

/*
 * Return true when the current HTTP response body ended before its declared
 * Content-Length was received. A valid, shorter-than-total 206 range response
 * is not an error if its own response body was received completely.
 */
ICACHE_FLASH_ATTR
static bool ota_incomplete_http_response(void) {
	return headers_parsed &&
		ota_response_length_known &&
		ota_response_received_bytes < ota_response_length;
}

/*
 * Reconstruct the SHA-256 state from the already-written firmware prefix.
 *
 * Network failures rewind to a 4KB sector boundary, so the byte count passed
 * here is aligned for flash reads. A zero-length prefix also reinitializes the
 * hash after a retry that rewound all the way to byte zero.
 */
ICACHE_FLASH_ATTR
static bool ota_rehash_flash(void) {
	uint32_t addr;
	uint32_t chunk_len;
	uint32_t buffer[64];	// Must be 32-bit aligned for spi_flash_read hardware requirements
	uint32_t end_addr;

	if (!has_sha256_check_1 && !has_sha256_check_2) {
		return true;
	}

	sha256_init(&ota_sha_ctx);

	addr = ota_target_base_addr;
	end_addr = ota_target_base_addr + ota_received_bytes;

	while (addr < end_addr) {
		chunk_len = sizeof(buffer);

		if (addr + chunk_len > end_addr) {
			chunk_len = end_addr - addr;
		}

		if (spi_flash_read(addr, buffer, chunk_len) != SPI_FLASH_RESULT_OK) {
#ifdef DEBUG
			os_printf("[OTA] ERROR: Flash read failed while rebuilding SHA-256 at 0x%08X!\n", addr);
#endif
			return false;
		}

		sha256_update(&ota_sha_ctx, (uint8_t *)buffer, chunk_len);
		addr += chunk_len;
	}

	return true;
}

ICACHE_FLASH_ATTR
static void ota_retry_timer_cb(void *arg) {
	err_t err;
	bool continuing_range;

	if (!ota_in_progress) {
		return;
	}

	continuing_range = ota_continue_pending;
	ota_continue_pending = false;
	ota_retry_pending = false;

	headers_parsed = false;
	/*
	 * A successful partial 206 response keeps the existing rBoot writer and
	 * hash stream alive. A failed attempt must restart the writer after its
	 * received-byte count has been rewound to a sector boundary.
	 */
	if (!continuing_range) {
		is_valid_binary = false;
	}
	ota_header_len = 0;

	ota_response_length = 0;
	ota_response_received_bytes = 0;
	ota_response_length_known = false;
	ota_response_status_code = 0;
	ota_response_range_end = 0;
	ota_response_range_total = 0;
	ota_response_range_valid = false;

	memset(ota_header_buffer, 0, sizeof(ota_header_buffer));
	memset(&ota_conn, 0, sizeof(ota_conn));
	memset(&ota_tcp, 0, sizeof(ota_tcp));

	ota_conn.type = ESPCONN_TCP;
	ota_conn.state = ESPCONN_NONE;
	ota_conn.proto.tcp = &ota_tcp;

	os_timer_disarm(&ota_timeout_timer);
	os_timer_setfn(&ota_timeout_timer, (os_timer_func_t *)ota_timeout_timer_cb, NULL);
	os_timer_arm(&ota_timeout_timer, OTA_NETWORK_TIMEOUT_MS, 0);

	ota_dns_pending = true;
	err = dns_gethostbyname(ota_host, &ota_ip, ota_dns_found_cb, &ota_conn);

	if (err == ERR_OK) {
		ota_dns_found_cb(ota_host, &ota_ip, &ota_conn);
	} else if (err != ERR_INPROGRESS) {
		ota_dns_pending = false;
		os_timer_disarm(&ota_timeout_timer);
		ota_trigger_reconnect(OTA_RETRY_FAILURE_DNS);
	}
}

/*
 * Schedule the next HTTP range after a complete, valid partial 206 response.
 * This is normal continuation, not a network retry: preserve the exact byte
 * offset, the rBoot writer state, and the active SHA-256 stream.
 */
ICACHE_FLASH_ATTR
static void ota_schedule_range_continue(void) {
	if (!ota_in_progress || ota_retry_pending) {
		return;
	}

	ota_dns_pending = false;
	ota_continue_pending = true;
	ota_retry_pending = true;
	os_timer_disarm(&ota_timeout_timer);
	os_timer_disarm(&ota_retry_timer);
	os_timer_setfn(&ota_retry_timer, (os_timer_func_t *)ota_retry_timer_cb, NULL);
	os_timer_arm(&ota_retry_timer, OTA_RECONNECT_DELAY_MS, 0);

#ifdef DEBUG
	os_printf("[OTA] Continuing partial HTTP range at byte offset %u...\n",
		ota_received_bytes);
#endif
}

ICACHE_FLASH_ATTR
static void ota_trigger_reconnect(ota_retry_reason_t reason) {
	char status_buf[64];
	bool reported_failure = false;

	if (!ota_in_progress || ota_retry_pending) {
		return;
	}

	os_timer_disarm(&ota_timeout_timer);
	ota_dns_pending = false;
	ota_continue_pending = false;

	switch (reason) {
		case OTA_RETRY_FAILURE_DNS:
			ota_dns_failure_seen = true;
			break;

		case OTA_RETRY_FAILURE_TCP:
			ota_tcp_failure_seen = true;
			break;

		case OTA_RETRY_FAILURE_INCOMPLETE_HTTP_RESPONSE:
			ota_incomplete_http_response_seen = true;
			break;

		case OTA_RETRY_FAILURE_OTHER:
		default:
			ota_other_failure_seen = true;
			break;
	}

	if (ota_retry_count < OTA_MAX_RETRIES) {
		ota_retry_count++;
		ota_retry_pending = true;

		// Rewind to the start of the sector that may contain incomplete data.
		ota_received_bytes &= ~(SPI_SECTOR_SIZE - 1);

#ifdef DEBUG
		os_printf("[OTA] Network drop! Attempting resume %d/%d from byte offset %u...\n",
			ota_retry_count, OTA_MAX_RETRIES, ota_received_bytes);
#endif
		/*
		 * Only report HTTP chunk retry progress for incomplete HTTP responses.
		 * DNS and TCP failures are reported if the OTA operation ultimately
		 * exhausts its retries.
		 */
		if (reason == OTA_RETRY_FAILURE_INCOMPLETE_HTTP_RESPONSE &&
			ota_mqtt_client) {
			tfp_snprintf(status_buf, sizeof(status_buf),
				"retrying_chunk_%d_offset_%u",
				ota_retry_count, ota_received_bytes);
			mqtt_rpc_ota_status(ota_mqtt_client, status_buf);
		}

		os_timer_disarm(&ota_retry_timer);
		os_timer_setfn(&ota_retry_timer, (os_timer_func_t *)ota_retry_timer_cb, NULL);
		os_timer_arm(&ota_retry_timer, OTA_RECONNECT_DELAY_MS, 0);
	} else {
#ifdef DEBUG
		os_printf("[OTA] ERROR: Max retries (%d) exceeded!\n", OTA_MAX_RETRIES);
#endif
		ota_cleanup_timers();
		ota_retry_pending = false;
		ota_continue_pending = false;
		is_valid_binary = false;
		ota_dns_pending = false;

		/*
		 * Report every failure category encountered during this unsuccessful
		 * OTA operation. A generic failure is always reported if no category
		 * was recorded, so retry exhaustion cannot end silently.
		 */
		if (ota_mqtt_client) {
			if (ota_dns_failure_seen) {
				mqtt_rpc_ota_status(
					ota_mqtt_client,
					"error_dns_resolution"
				);
				reported_failure = true;
			}

			if (ota_tcp_failure_seen) {
				mqtt_rpc_ota_status(
					ota_mqtt_client,
					"error_tcp_connection"
				);
				reported_failure = true;
			}

			if (ota_incomplete_http_response_seen) {
				mqtt_rpc_ota_status(
					ota_mqtt_client,
					"error_incomplete_http_response"
				);
				reported_failure = true;
			}

			if (ota_other_failure_seen) {
				mqtt_rpc_ota_status(
					ota_mqtt_client,
					"error_incomplete_firmware"
				);
				reported_failure = true;
			}

			if (!reported_failure) {
				mqtt_rpc_ota_status(
					ota_mqtt_client,
					"error_retry_exhausted"
				);
			}
		}

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
	const char *cl;
	const char *cr;
	uint32_t parsed_content_length;
	uint32_t range_start;
	uint32_t range_end;
	uint32_t range_total;
	uint32_t range_length;
	uint16_t copy_len;
	uint16_t previous_header_len;
	uint16_t total_header_size;
	uint16_t body_offset;
	int status_code;
	bool cl_duplicate;
	bool cr_duplicate;

	if (!ota_in_progress || ota_retry_pending) {
		return;
	}

	// Reset network inactivity window on every packet received.
	os_timer_disarm(&ota_timeout_timer);
	os_timer_arm(&ota_timeout_timer, OTA_NETWORK_TIMEOUT_MS, 0);

	pdata = pusrdata;
	len = length;

	if (!headers_parsed) {
		/*
		 * Append the packet to the header buffer. If the header terminator is
		 * found, process the body directly from pusrdata so body bytes aren't
		 * lost if the packet is larger than the remaining header-buffer space.
		 */
		previous_header_len = ota_header_len;
		copy_len = length;

		if ((uint32_t)ota_header_len + copy_len > OTA_MAX_HEADER_SIZE - 1) {
			copy_len = OTA_MAX_HEADER_SIZE - 1 - ota_header_len;
		}

		os_memcpy(ota_header_buffer + ota_header_len, pusrdata, copy_len);
		ota_header_len += copy_len;
		ota_header_buffer[ota_header_len] = '\0';

		body_marker = (char *)os_strstr(ota_header_buffer, "\r\n\r\n");

		if (!body_marker) {
			if (ota_header_len >= OTA_MAX_HEADER_SIZE - 1) {
#ifdef DEBUG
				os_printf("[OTA] ERROR: Header overflow!\n");
#endif
				ota_fail_update("error_header_overflow", true);
			}

			return;
		}

		/*
		 * Validate the exact HTTP status code from the status line. Do not
		 * search the entire header for arbitrary occurrences of "200" or "206".
		 */
		if (!ota_parse_status_code(&status_code)) {
#ifdef DEBUG
			os_printf("[OTA] ERROR: Invalid HTTP status line!\n");
#endif
			ota_fail_update("error_invalid_http_header", true);
			return;
		}

		if (status_code != 200 && status_code != 206) {
#ifdef DEBUG
			os_printf("[OTA] ERROR: HTTP status %d received!\n", status_code);
#endif
			ota_fail_update("error_http_not_200", true);
			return;
		}

		// Require an unambiguous Content-Length for reliable completion checks.
		cl = ota_get_header_value(ota_header_buffer, "Content-Length", &cl_duplicate);

		if (!cl || cl_duplicate ||
			!ota_parse_uint32(cl, &parsed_content_length) ||
			parsed_content_length == 0) {
#ifdef DEBUG
			os_printf("[OTA] ERROR: Missing, duplicated, or invalid Content-Length!\n");
#endif
			ota_fail_update("error_invalid_content_length", true);
			return;
		}

		ota_response_status_code = status_code;
		ota_response_range_end = 0;
		ota_response_range_total = 0;
		ota_response_range_valid = false;

		if (status_code == 200) {
			/*
			 * A server may ignore Range and return the entire file. Restart
			 * the logical download, writer, and hash stream from byte zero.
			 */
			if (parsed_content_length < MIN_FIRMWARE_SIZE ||
				parsed_content_length > MAX_FIRMWARE_SIZE) {
#ifdef DEBUG
				os_printf("[OTA] ERROR: Invalid firmware size %u bytes!\n",
					parsed_content_length);
#endif
				ota_fail_update("error_invalid_firmware_size", true);
				return;
			}

			if (ota_request_offset > 0) {
#ifdef DEBUG
				os_printf("[OTA] Server ignored Range; restarting from byte zero.\n");
#endif
			}

			ota_received_bytes = 0;
			ota_request_offset = 0;
			ota_response_received_bytes = 0;
			is_valid_binary = false;

			ota_firmware_size = parsed_content_length;
			ota_firmware_size_known = true;

			/*
			 * A full response starts a new hash stream. Do not retain any
			 * hash state from the previous partial attempt.
			 */
			if (has_sha256_check_1 || has_sha256_check_2) {
				sha256_init(&ota_sha_ctx);
			}
		} else {
			/*
			 * A partial response must describe exactly the requested range.
			 * Content-Length is the response-body length, not the total
			 * firmware size.
			 */
			cr = ota_get_header_value(ota_header_buffer, "Content-Range", &cr_duplicate);

			if (!cr || cr_duplicate ||
				!ota_parse_content_range(cr, &range_start, &range_end, &range_total)) {
#ifdef DEBUG
				os_printf("[OTA] ERROR: Missing, duplicated, or invalid Content-Range!\n");
#endif
				ota_fail_update("error_invalid_content_range", true);
				return;
			}

			if (range_start != ota_request_offset ||
				range_end < range_start ||
				range_total == 0 ||
				range_end >= range_total) {
#ifdef DEBUG
				os_printf("[OTA] ERROR: Content-Range does not match requested offset!\n");
#endif
				ota_fail_update("error_invalid_content_range", true);
				return;
			}

			if (range_total < MIN_FIRMWARE_SIZE ||
				range_total > MAX_FIRMWARE_SIZE) {
#ifdef DEBUG
				os_printf("[OTA] ERROR: Invalid total firmware size %u bytes!\n", range_total);
#endif
				ota_fail_update("error_invalid_firmware_size", true);
				return;
			}

			range_length = range_end - range_start + 1;

			if (parsed_content_length != range_length) {
#ifdef DEBUG
				os_printf("[OTA] ERROR: Content-Length does not match Content-Range!\n");
#endif
				ota_fail_update("error_invalid_content_range", true);
				return;
			}

			if (ota_firmware_size_known && ota_firmware_size != range_total) {
#ifdef DEBUG
				os_printf("[OTA] ERROR: Firmware size changed between responses!\n");
#endif
				ota_fail_update("error_invalid_content_range", true);
				return;
			}

			ota_firmware_size = range_total;
			ota_firmware_size_known = true;
			ota_response_range_end = range_end;
			ota_response_range_total = range_total;
			ota_response_range_valid = true;
		}

		ota_response_length = parsed_content_length;
		ota_response_received_bytes = 0;
		ota_response_length_known = true;

#ifdef DEBUG
		os_printf("[OTA] HTTP %d, response size %u bytes, firmware size %u bytes, offset %u\n",
			status_code, ota_response_length, ota_firmware_size, ota_received_bytes);
#endif

		headers_parsed = true;

		total_header_size = (uint16_t)((body_marker + 4) - ota_header_buffer);

		if (total_header_size < previous_header_len) {
			ota_fail_update("error_invalid_http_header", true);
			return;
		}

		body_offset = total_header_size - previous_header_len;

		if (body_offset > length) {
			ota_fail_update("error_invalid_http_header", true);
			return;
		}

		len = length - body_offset;
		pdata = pusrdata + body_offset;
	}

	if (!headers_parsed || len == 0) {
		return;
	}

	// Reject extra bytes beyond the declared body length.
	if (!ota_response_length_known ||
		ota_response_received_bytes > ota_response_length ||
		len > ota_response_length - ota_response_received_bytes) {
#ifdef DEBUG
		os_printf("[OTA] ERROR: Received more data than declared Content-Length!\n");
#endif
		ota_fail_update("error_response_too_large", true);
		return;
	}

	// Never allow the image data to exceed the known firmware size.
	if (!ota_firmware_size_known ||
		ota_received_bytes > ota_firmware_size ||
		len > ota_firmware_size - ota_received_bytes ||
		ota_received_bytes + len > MAX_FIRMWARE_SIZE) {
#ifdef DEBUG
		os_printf("[OTA] ERROR: Firmware image exceeds its validated size!\n");
#endif
		ota_fail_update("error_file_too_large", true);
		return;
	}

	// Validate magic byte on the first firmware byte.
	if (!is_valid_binary) {
		magic_byte = (uint8_t)pdata[0];

		if (ota_received_bytes == 0 &&
			magic_byte != ESP_IMAGE_MAGIC_V1 &&
			magic_byte != ESP_IMAGE_MAGIC_V2) {
#ifdef DEBUG
			os_printf("[OTA] ERROR: Invalid magic byte 0x%02X!\n", magic_byte);
#endif
			ota_fail_update("error_invalid_magic", true);
			return;
		}

		is_valid_binary = true;

		// Look up the flash memory address for the target ROM slot.
		conf = rboot_get_config();
		ota_target_base_addr = conf.roms[ota_target_rom];

		/*
		 * Erase the sector at the write position. Failed transfers rewind to a
		 * sector boundary; a clean, partial 206 continuation keeps the active
		 * writer alive and therefore does not erase or reinitialize this sector.
		 */
		current_flash_addr = ota_target_base_addr + ota_received_bytes;
		sector_to_erase = current_flash_addr / SPI_SECTOR_SIZE;

		if (spi_flash_erase_sector(sector_to_erase) != SPI_FLASH_RESULT_OK) {
#ifdef DEBUG
			os_printf("[OTA] ERROR: Flash erase failed at sector %u!\n",
				sector_to_erase);
#endif
			ota_fail_update("error_flash_erase", true);
			return;
		}

		// Initialize the rBoot flash writing engine at the current offset.
		ota_status = rboot_write_init(current_flash_addr);

#ifdef DEBUG
		os_printf("[OTA] Flashing to ROM %d (0x%08X) offset %u...\n",
			ota_target_rom, ota_target_base_addr, ota_received_bytes);
#endif

		// Always rebuild the hash, including when the rewind offset is zero.
		if ((has_sha256_check_1 || has_sha256_check_2) &&
			!ota_rehash_flash()) {
			ota_fail_update("error_flash_read", true);
			return;
		}
	}

	// Hash bytes before writing; a failed update will never be staged for boot.
	if (has_sha256_check_1 || has_sha256_check_2) {
		sha256_update(&ota_sha_ctx, (uint8_t *)pdata, len);
	}

	if (!rboot_write_flash(&ota_status, (uint8_t *)pdata, len)) {
#ifdef DEBUG
		os_printf("[OTA] ERROR: Flash write failed at offset %u!\n", ota_received_bytes);
#endif
		ota_fail_update("error_flash_write", true);
		return;
	}

	ota_received_bytes += len;
	ota_response_received_bytes += len;
}

ICACHE_FLASH_ATTR
static void ota_tcp_recon_cb(void *arg, sint8 err) {
	bool incomplete_http_response;

	if (!ota_in_progress || ota_retry_pending) {
		return;
	}

	os_timer_disarm(&ota_timeout_timer);

#ifdef DEBUG
	os_printf("[OTA] TCP connection error (%d)\n", err);
#endif

	/*
	 * A TCP connection error is reportable even if it interrupted an HTTP
	 * response. Track the incomplete response separately so both failure
	 * details can be reported if the OTA operation ultimately fails.
	 */
	ota_tcp_failure_seen = true;
	incomplete_http_response = ota_incomplete_http_response();

	if (incomplete_http_response) {
		ota_trigger_reconnect(OTA_RETRY_FAILURE_INCOMPLETE_HTTP_RESPONSE);
	} else {
		ota_trigger_reconnect(OTA_RETRY_FAILURE_TCP);
	}
}

ICACHE_FLASH_ATTR
static void ota_tcp_discon_cb(void *arg) {
	char status_msg[32];
	uint8_t calculated_sha256[32];
	bool matched;
	bool incomplete_http_response;

	if (!ota_in_progress || ota_retry_pending) {
		return;
	}

	os_timer_disarm(&ota_timeout_timer);

	/*
	 * Completion requires the current HTTP response body to match its
	 * Content-Length and the full image to match the total firmware size.
	 */
	if (is_valid_binary &&
		ota_response_length_known &&
		ota_response_received_bytes == ota_response_length &&
		ota_firmware_size_known &&
		ota_received_bytes == ota_firmware_size) {

		ota_cleanup_timers();

		if (!rboot_write_end(&ota_status)) {
#ifdef DEBUG
			os_printf("[OTA] ERROR: Failed to finalize flash write!\n");
#endif
			ota_fail_update("error_flash_write", false);
			return;
		}

		// Reject unexpectedly small images even after the HTTP size checks.
		if (ota_received_bytes < MIN_FIRMWARE_SIZE) {
#ifdef DEBUG
			os_printf("[OTA] ERROR: Downloaded image too small (%u bytes)\n",
				ota_received_bytes);
#endif
			ota_fail_update("error_too_small", false);
			return;
		}

		// Verify the actual downloaded size against the validated image size.
		if (ota_received_bytes != ota_firmware_size) {
#ifdef DEBUG
			os_printf("[OTA] ERROR: Size mismatch. Expected %u, got %u\n",
				ota_firmware_size, ota_received_bytes);
#endif
			ota_fail_update("error_truncated", false);
			return;
		}

		// Verify SHA-256 against the hash for the target ROM slot.
		if (has_sha256_check_1 || has_sha256_check_2) {
			matched = false;
			sha256_final(&ota_sha_ctx, calculated_sha256);

			if (ota_target_rom == 0 && has_sha256_check_1) {
				if (os_memcmp(calculated_sha256, expected_sha256_1, 32) == 0) {
					matched = true;
				}
			} else if (ota_target_rom == 1 && has_sha256_check_2) {
				if (os_memcmp(calculated_sha256, expected_sha256_2, 32) == 0) {
					matched = true;
				}
			}

			if (!matched) {
#ifdef DEBUG
				os_printf("[OTA] ERROR: SHA-256 checksum mismatch for target ROM slot %d!\n",
					ota_target_rom);
#endif
				ota_fail_update("error_checksum_mismatch", false);
				return;
			}

#ifdef DEBUG
			os_printf("[OTA] SUCCESS: SHA-256 verified for ROM slot %d.\n",
				ota_target_rom);
#endif
		}

		/*
		 * Stage a temporary boot to the target slot. rBoot can roll back if
		 * the new firmware fails its boot/self-test process.
		 */
		if (!rboot_set_temp_rom(ota_target_rom)) {
#ifdef DEBUG
			os_printf("[OTA] ERROR: Failed to stage temporary boot ROM!\n");
#endif
			ota_fail_update("error_boot_stage", false);
			return;
		}

#ifdef DEBUG
		os_printf("[OTA] Download complete. Staging temp boot to ROM %d...\n",
			ota_target_rom);
#endif

		if (ota_mqtt_client) {
			tfp_snprintf(status_msg, sizeof(status_msg),
				"success&target_rom=%d", ota_target_rom + 1);
			mqtt_rpc_ota_status(ota_mqtt_client, status_msg);

			tfp_snprintf(status_msg, sizeof(status_msg),
				"restarting_in_%ds",
				(OTA_REBOOT_DELAY_MS + SYSTEM_RESTART_DELAY_MS) / 1000);
			mqtt_rpc_ota_status(ota_mqtt_client, status_msg);
		}

		// Allow the MQTT stack time to flush its queued messages before reboot.
		os_timer_disarm(&ota_reboot_timer);
		os_timer_setfn(&ota_reboot_timer,
			(os_timer_func_t *)ota_reboot_timer_cb, NULL);
		os_timer_arm(&ota_reboot_timer, OTA_REBOOT_DELAY_MS, 0);

	} else {
#ifdef DEBUG
		os_printf("[OTA] HTTP response ended before the complete firmware was received.\n");
#endif
		/*
		 * An incomplete response body is a failed transfer and consumes a
		 * retry. A complete 206 response that intentionally covers only part
		 * of the image is normal: request the next range without spending a
		 * retry or resetting the flash writer/hash stream.
		 */
		incomplete_http_response = ota_incomplete_http_response();

		if (incomplete_http_response) {
			ota_trigger_reconnect(OTA_RETRY_FAILURE_INCOMPLETE_HTTP_RESPONSE);
		} else if (!headers_parsed || !ota_response_length_known) {
			// The connection closed before a usable HTTP response was received.
			ota_tcp_failure_seen = true;
			ota_trigger_reconnect(OTA_RETRY_FAILURE_TCP);
		} else if (ota_response_received_bytes == ota_response_length &&
			ota_response_status_code == 206 &&
			ota_response_range_valid &&
			ota_response_range_end + 1 < ota_response_range_total &&
			ota_received_bytes == ota_response_range_end + 1 &&
			ota_received_bytes < ota_firmware_size) {
			ota_schedule_range_continue();
		} else {
			ota_trigger_reconnect(OTA_RETRY_FAILURE_OTHER);
		}
	}
}

ICACHE_FLASH_ATTR
static void ota_tcp_connect_cb(void *arg) {
	char request[512];
	sint8 err;

	if (!ota_in_progress || ota_retry_pending) {
		return;
	}

	ota_dns_pending = false;
	ota_request_offset = ota_received_bytes;
	ota_response_received_bytes = 0;
	ota_response_length = 0;
	ota_response_length_known = false;
	ota_response_status_code = 0;
	ota_response_range_end = 0;
	ota_response_range_total = 0;
	ota_response_range_valid = false;

	if (ota_request_offset > 0) {
		// Request the remaining bytes starting at the current accepted offset.
		tfp_snprintf(request, sizeof(request),
			"GET %s HTTP/1.1\r\n"
			"Host: %s\r\n"
			"Range: bytes=%u-\r\n"
			"Connection: close\r\n"
			"User-Agent: ESP8266-OTA\r\n"
			"\r\n",
			ota_path, ota_host_header, ota_request_offset);
	} else {
		// Request the whole image.
		tfp_snprintf(request, sizeof(request),
			"GET %s HTTP/1.0\r\n"
			"Host: %s\r\n"
			"Connection: close\r\n"
			"User-Agent: ESP8266-OTA\r\n"
			"\r\n",
			ota_path, ota_host_header);
	}

	err = espconn_send(&ota_conn, (uint8_t *)request, strlen(request));

	if (err != ESPCONN_OK) {
#ifdef DEBUG
		os_printf("[OTA] ERROR: Failed to send HTTP request (%d)!\n", err);
#endif
		os_timer_disarm(&ota_timeout_timer);
		ota_trigger_reconnect(OTA_RETRY_FAILURE_TCP);
		espconn_abort(&ota_conn);
	}
}

ICACHE_FLASH_ATTR
static void ota_dns_found_cb(const char *name, ip_addr_t *ipaddr, void *arg) {
	sint8 err;

	if (!ota_in_progress || ota_retry_pending) {
		return;
	}

	ota_dns_pending = false;

	if (ipaddr == NULL) {
		os_timer_disarm(&ota_timeout_timer);

#ifdef DEBUG
		os_printf("[OTA] ERROR: DNS resolution failed!\n");
#endif

		ota_trigger_reconnect(OTA_RETRY_FAILURE_DNS);
		return;
	}

	os_memcpy(ota_conn.proto.tcp->remote_ip, &ipaddr->addr, 4);
	ota_conn.proto.tcp->remote_port = ota_port;
	ota_conn.proto.tcp->local_port = espconn_port();

	espconn_regist_connectcb(&ota_conn, ota_tcp_connect_cb);
	espconn_regist_recvcb(&ota_conn, ota_tcp_recv_cb);
	espconn_regist_disconcb(&ota_conn, ota_tcp_discon_cb);
	espconn_regist_reconcb(&ota_conn, ota_tcp_recon_cb);

	err = espconn_connect(&ota_conn);

	if (err != ESPCONN_OK) {
#ifdef DEBUG
		os_printf("[OTA] ERROR: Failed to start TCP connection (%d)!\n", err);
#endif
		os_timer_disarm(&ota_timeout_timer);
		ota_trigger_reconnect(OTA_RETRY_FAILURE_TCP);
	}
}

ICACHE_FLASH_ATTR
bool start_ota_update(
	MQTT_Client *client,
	const char *url,
	const char *hex_sha256_1,
	const char *hex_sha256_2,
	uint8_t *out_target_rom
) {
	const char *p;
	const char *slash;
	const char *question;
	const char *colon;
	const char *authority_end;
	const char *path_start;
	const char *path_end;
	const char *fragment;
	const char *port_char;
	size_t host_len;
	size_t path_len;
	size_t port_len;
	static char ota_url[256];
	char port_buffer[6];
	uint32_t parsed_port;
	uint8_t current_rom;
	err_t err;

	ota_mqtt_client = client;

	ota_retry_count = 0;
	ota_retry_pending = false;
	ota_continue_pending = false;
	ota_dns_pending = false;
	ota_incomplete_http_response_seen = false;
	ota_dns_failure_seen = false;
	ota_tcp_failure_seen = false;
	ota_other_failure_seen = false;

	ota_firmware_size = 0;
	ota_firmware_size_known = false;
	ota_response_length = 0;
	ota_response_received_bytes = 0;
	ota_response_length_known = false;
	ota_response_status_code = 0;
	ota_response_range_end = 0;
	ota_response_range_total = 0;
	ota_response_range_valid = false;
	ota_request_offset = 0;

	ota_received_bytes = 0;

	headers_parsed = false;
	is_valid_binary = false;
	ota_header_len = 0;

	memset(ota_header_buffer, 0, sizeof(ota_header_buffer));

	os_timer_disarm(&ota_reboot_timer);
	ota_cleanup_timers();

	ota_in_progress = true;

	os_timer_setfn(&ota_progress_timer,
		(os_timer_func_t *)ota_progress_timer_cb, NULL);
	os_timer_arm(&ota_progress_timer, OTA_PROGRESS_INTERVAL_MS, 1);

	if (!ota_parse_sha256(hex_sha256_1, expected_sha256_1,
		&has_sha256_check_1) ||
		!ota_parse_sha256(hex_sha256_2, expected_sha256_2,
			&has_sha256_check_2)) {
#ifdef DEBUG
		os_printf("[OTA] ERROR: Invalid SHA-256 digest; expected 64 hexadecimal characters.\n");
#endif
		ota_fail_update("error_invalid_sha256", false);
		return false;
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

	// Reject URLs that cannot fit in the parsing buffer.
	if (!url || strlen(url) >= sizeof(ota_url)) {
#ifdef DEBUG
		os_printf("[OTA] ERROR: URL is missing or too long!\n");
#endif
		ota_fail_update("error_invalid_url", false);
		return false;
	}

	strcpy(ota_url, url);

#ifdef DEBUG
	os_printf("[OTA] Request URL: %s\n", ota_url);
#endif

	// Parse HTTP URLs. HTTPS is intentionally unsupported by this client.
	p = ota_url;

	if (strncmp(p, "http://", 7) == 0) {
		p += 7;
	} else if (strncmp(p, "https://", 8) == 0) {
#ifdef DEBUG
		os_printf("[OTA] ERROR: HTTPS is not supported by this OTA client!\n");
#endif
		ota_fail_update("error_invalid_url", false);
		return false;
	} else if (strstr(p, "://") != NULL) {
		ota_fail_update("error_invalid_url", false);
		return false;
	}

	// The authority ends at the first path or query delimiter.
	slash = strchr(p, '/');
	question = strchr(p, '?');
	fragment = strchr(p, '#');
	authority_end = p + strlen(p);

	if (slash && slash < authority_end) {
		authority_end = slash;
	}

	if (question && question < authority_end) {
		authority_end = question;
	}

	if (fragment && fragment < authority_end) {
		authority_end = fragment;
	}

	colon = NULL;
	for (port_char = p; port_char < authority_end; port_char++) {
		if (*port_char == ':') {
			if (colon) {
				ota_fail_update("error_invalid_url", false);
				return false;
			}
			colon = port_char;
		}
	}

	if (colon) {
		host_len = colon - p;
		port_len = authority_end - colon - 1;

		if (host_len == 0 || host_len >= sizeof(ota_host) ||
			port_len == 0 || port_len > 5) {
			ota_fail_update("error_invalid_url", false);
			return false;
		}

		memcpy(ota_host, p, host_len);
		ota_host[host_len] = '\0';

		memcpy(port_buffer, colon + 1, port_len);
		port_buffer[port_len] = '\0';

		parsed_port = 0;
		for (port_char = port_buffer; *port_char; port_char++) {
			if (*port_char < '0' || *port_char > '9') {
				ota_fail_update("error_invalid_url", false);
				return false;
			}

			parsed_port = parsed_port * 10UL + (uint32_t)(*port_char - '0');
			if (parsed_port > 65535UL) {
				ota_fail_update("error_invalid_url", false);
				return false;
			}
		}

		if (parsed_port == 0 || parsed_port > 65535UL) {
			ota_fail_update("error_invalid_url", false);
			return false;
		}

		ota_port = (int)parsed_port;
		tfp_snprintf(ota_host_header, sizeof(ota_host_header), "%s:%d",
			ota_host, ota_port);
	} else {
		host_len = authority_end - p;

		if (host_len == 0 || host_len >= sizeof(ota_host)) {
			ota_fail_update("error_invalid_url", false);
			return false;
		}

		memcpy(ota_host, p, host_len);
		ota_host[host_len] = '\0';
		ota_port = OTA_DEFAULT_PORT;
		tfp_snprintf(ota_host_header, sizeof(ota_host_header), "%s", ota_host);
	}

	/*
	 * Preserve paths and query strings. A query directly after the host is
	 * sent as "/?query"; URL fragments are not part of an HTTP request target.
	 */
	path_start = NULL;
	if (slash && slash < (fragment ? fragment : ota_url + strlen(ota_url)) &&
		(!question || slash < question)) {
		path_start = slash;
	} else if (question &&
		question < (fragment ? fragment : ota_url + strlen(ota_url))) {
		path_start = question;
	}

	if (!path_start) {
		strcpy(ota_path, "/");
	} else {
		path_end = ota_url + strlen(ota_url);
		fragment = strchr(path_start, '#');
		if (fragment) {
			path_end = fragment;
		}

		path_len = path_end - path_start;
		if (path_len + (path_start == question ? 1 : 0) >= sizeof(ota_path)) {
#ifdef DEBUG
			os_printf("[OTA] ERROR: URL path is too long!\n");
#endif
			ota_fail_update("error_invalid_url", false);
			return false;
		}

		if (path_start == question) {
			ota_path[0] = '/';
			memcpy(ota_path + 1, path_start, path_len);
			ota_path[path_len + 1] = '\0';
		} else {
			memcpy(ota_path, path_start, path_len);
			ota_path[path_len] = '\0';
		}
	}

#ifdef DEBUG
	os_printf("[OTA] Parsed Host: %s | Port: %d | Path: %s\n",
		ota_host, ota_port, ota_path);
#endif

	// Initialize the TCP connection.
	memset(&ota_conn, 0, sizeof(ota_conn));
	memset(&ota_tcp, 0, sizeof(ota_tcp));

	ota_conn.type = ESPCONN_TCP;
	ota_conn.state = ESPCONN_NONE;
	ota_conn.proto.tcp = &ota_tcp;

	// Arm the timeout before DNS resolution and connection setup.
	os_timer_disarm(&ota_timeout_timer);
	os_timer_setfn(&ota_timeout_timer,
		(os_timer_func_t *)ota_timeout_timer_cb, NULL);
	os_timer_arm(&ota_timeout_timer, OTA_NETWORK_TIMEOUT_MS, 0);

	ota_dns_pending = true;
	err = dns_gethostbyname(ota_host, &ota_ip, ota_dns_found_cb, &ota_conn);

	if (err == ERR_OK) {
		ota_dns_found_cb(ota_host, &ota_ip, &ota_conn);
	} else if (err != ERR_INPROGRESS) {
#ifdef DEBUG
		os_printf("[OTA] ERROR: DNS request could not be started (%d)!\n", err);
#endif
		ota_dns_pending = false;
		os_timer_disarm(&ota_timeout_timer);
		ota_trigger_reconnect(OTA_RETRY_FAILURE_DNS);
	}

	return true;
}
