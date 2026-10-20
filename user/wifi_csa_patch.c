#include "ets_sys.h"
#include "osapi.h"
#include "c_types.h"
#include "user_interface.h"
#include <stdbool.h>

// Fallback macro definition if ICACHE_RAM_ATTR is not defined in SDK headers
#ifndef ICACHE_RAM_ATTR
#define ICACHE_RAM_ATTR __attribute__((section(".iram1.text")))
#endif

// 802.11 Management MAC Header (24 bytes)
typedef struct {
	uint16_t frame_control;
	uint16_t duration;
	uint8_t  da[6];        // Address 1: Destination Address
	uint8_t  sa[6];        // Address 2: Source Address / Transmitter BSSID
	uint8_t  bssid[6];     // Address 3: BSSID
	uint16_t seq_ctrl;
} __attribute__((packed)) ieee80211_mgmt_hdr_t;

// Global tracking variables exposed to user_main.c
uint32_t cnx_csa_call_count = 0;
bool cnx_csa_new_call_flag = false;

// Global tracking variables for captured CSA sender BSSID
uint8_t cnx_csa_last_bssid[6] = {0};
bool cnx_csa_bssid_captured = false;

// Internal scan state tracking
static volatile bool is_scanning = false;
static scan_done_cb_t user_scan_cb = NULL;

// References to real functions wrapped by GNU linker
extern const char * __real_system_get_sdk_version(void);
extern void __real_cnx_csa_fn(void *arg);
extern bool __real_wifi_station_scan(struct scan_config *config, scan_done_cb_t cb);
extern void __real_ieee80211_parse_beacon(void *ni, const uint8_t *frame_buf, uint16_t len);

// Helper function to inspect Information Elements for Channel Switch Announcement (IE ID 37 / 0x25)
// MUST be ICACHE_RAM_ATTR because it is called directly from the Wi-Fi receive ISR context!
static ICACHE_RAM_ATTR bool parse_csa_ie(const uint8_t *payload, uint16_t len, uint8_t *out_new_channel) {
	uint16_t offset;

	if (payload == NULL || len < 36) {
		return false; // Minimum 24B MAC Header + 12B Fixed Parameters (Timestamp, Interval, CapInfo)
	}

	offset = 36; // Start of Information Elements
	while (offset + 2 <= len) {
		uint8_t ie_id = payload[offset];
		uint8_t ie_len = payload[offset + 1];

		if (offset + 2 + ie_len > len) {
			break; // Truncated IE stream
		}

		// Check for Channel Switch Announcement IE (ID 37 / 0x25)
		if (ie_id == 37 && ie_len >= 3) {
			if (out_new_channel != NULL) {
				*out_new_channel = payload[offset + 3]; // New channel byte is at IE offset +3
			}
			return true;
		}

		offset += 2 + ie_len;
	}

	return false;
}

// Linker wrapper intercepting ieee80211_parse_beacon
void ICACHE_RAM_ATTR __wrap_ieee80211_parse_beacon(void *ni, const uint8_t *frame_buf, uint16_t len) {
	if (frame_buf != NULL && len >= sizeof(ieee80211_mgmt_hdr_t)) {
		const ieee80211_mgmt_hdr_t *hdr = (const ieee80211_mgmt_hdr_t *)frame_buf;
		uint8_t target_channel = 0;

		uint8_t type = (hdr->frame_control >> 2) & 0x03;
		uint8_t subtype = (hdr->frame_control >> 4) & 0x0F;

		// Type 0 = Management, Subtype 8 = Beacon, Subtype 5 = Probe Response
		if (type == 0 && (subtype == 8 || subtype == 5)) {
			if (parse_csa_ie(frame_buf, len, &target_channel)) {
				// Capture Source Address (Address 2) -> BSSID of the broadcasting AP
				os_memcpy(cnx_csa_last_bssid, hdr->sa, 6);
				cnx_csa_bssid_captured = true;

				#ifdef DEBUG
				os_printf("[CSA DETECTED] AP BSSID: %02x:%02x:%02x:%02x:%02x:%02x -> New Channel: %u\n",
					cnx_csa_last_bssid[0], cnx_csa_last_bssid[1], cnx_csa_last_bssid[2],
					cnx_csa_last_bssid[3], cnx_csa_last_bssid[4], cnx_csa_last_bssid[5],
					target_channel);
				#endif
			}
		}
	}

	// Always pass execution to the real SDK beacon parser
	__real_ieee80211_parse_beacon(ni, frame_buf, len);
}

// Intercepted scan callback to clear the scanning flag
static void ICACHE_FLASH_ATTR intercepted_scan_cb(void *arg, STATUS status) {
	scan_done_cb_t orig_cb;

	is_scanning = false; // Clear flag when scan completes or fails
	
	orig_cb = user_scan_cb;
	user_scan_cb = NULL;

	if (orig_cb != NULL) {
		orig_cb(arg, status);
	}
}

// Intercept the scan call to set the flag and inject our callback
bool ICACHE_FLASH_ATTR __wrap_wifi_station_scan(struct scan_config *config, scan_done_cb_t cb) {
	bool res;
	
	user_scan_cb = cb;
	is_scanning = true; // Set flag before starting scan

	res = __real_wifi_station_scan(config, intercepted_scan_cb);
	if (!res) {
		// Scan failed to start, clear flag immediately
		is_scanning = false;
		user_scan_cb = NULL;
	}
	
	return res;
}

void ICACHE_RAM_ATTR __wrap_cnx_csa_fn(void *arg) {
	// Increment counter and flag for MQTT/system logging
	cnx_csa_call_count++;
	cnx_csa_new_call_flag = true;

	if (cnx_csa_bssid_captured) {
		#ifdef DEBUG
		os_printf("cnx_csa_fn triggered by AP BSSID: %02x:%02x:%02x:%02x:%02x:%02x\n",
			cnx_csa_last_bssid[0], cnx_csa_last_bssid[1], cnx_csa_last_bssid[2],
			cnx_csa_last_bssid[3], cnx_csa_last_bssid[4], cnx_csa_last_bssid[5]);
		#endif
	}

	// If we are actively scanning, this CSA is almost certainly from a foreign AP on another channel.
	if (is_scanning) {
		#ifdef DEBUG
		os_printf("cnx_csa_fn: Blocked rogue CSA switch during active scan\n");
		#endif
		return;
	}

	// Allow valid CSA execution if we are just idling/connected normally
	__real_cnx_csa_fn(arg);
}

ICACHE_FLASH_ATTR
const char * __wrap_system_get_sdk_version(void) {
	static char patched_version[64];
	static bool is_cached = false;
	const char *orig;
	const char *paren;
	size_t prefix_len;

	if (is_cached) {
		return patched_version;
	}

	orig = __real_system_get_sdk_version();

	if (orig == NULL) {
		os_strcpy(patched_version, "unknown-patched");
		is_cached = true;
		return patched_version;
	}

	// Look for the starting parenthesis of the git commit hash e.g., "3.0.6-dev(072755c)"
	paren = os_strchr(orig, '(');

	if (paren != NULL) {
		// Calculate length of version string prior to '('
		prefix_len = (size_t)(paren - orig);

		if (prefix_len < sizeof(patched_version) - 16) {
			// Copy base version (e.g., "3.0.6-dev")
			os_memcpy(patched_version, orig, prefix_len);
			patched_version[prefix_len] = '\0';

			// Append "-patched" and the remaining string starting from "("
			os_sprintf(patched_version + prefix_len, "-patched%s", paren);
			is_cached = true;
			return patched_version;
		}
	} else {
		// Fallback: If no '(' exists (e.g., "3.0.6"), simply append "-patched" to the end
		os_sprintf(patched_version, "%s-patched", orig);
		is_cached = true;
		return patched_version;
	}

	// Safety fallback if buffer overflow protection triggers
	return orig;
}
