#include <esp8266.h>
#include <lwip/ip.h>
#include <lwip/udp.h>
#include <lwip/tcp_impl.h>
#include <netif/etharp.h>
#include <lwip/netif.h>
#include <lwip/lwip_napt.h>
#include <lwip/dns.h>
#include <lwip/app/ping.h>
#include <lwip/opt.h>
#include <espconn.h>

#include "icmp_ping.h"

#define MOVING_AVERAGE_BUFFER_SIZE 60

uint32_t network_average_response_time_ms = 0;
uint32_t network_response_time_error_count = 0;

uint32_t network_moving_average_buffer[MOVING_AVERAGE_BUFFER_SIZE];	// 1 hour moving average
uint8_t h = 0;
uint8_t t = 0;

struct ping_option ping_opt;

ICACHE_FLASH_ATTR
void dns_found(const char *name, ip_addr_t *ip, void *arg) {
	if (ip == NULL) {
#ifdef DEBUG
		printf("DNS: Found, but got no ip, try to reconnect\r\n");
#endif
		return;
	}

#ifdef DEBUG
	printf("DNS: found ip %d.%d.%d.%d\n",
		*((uint8 *) &ip->addr),
		*((uint8 *) &ip->addr + 1),
		*((uint8 *) &ip->addr + 2),
		*((uint8 *) &ip->addr + 3));
#endif

	icmp_ping(ip);
}

ICACHE_FLASH_ATTR
void icmp_ping_recv(void *arg, void *pdata) {
	struct ping_resp *ping_resp = pdata;
//	struct ping_option *ping_opt = arg;
	uint8_t ping_response_count;
	uint32_t network_response_time_sum = 0;
	uint32_t reponse_time = 0;
	uint8_t i;

	if (ping_resp->ping_err == -1) {
#ifdef DEBUG
		printf("ping host fail \r\n");
		network_response_time_error_count++;
#endif
	}
	else {
#ifdef DEBUG
		printf("ping recv: byte = %d, time = %d ms \r\n", ping_resp->bytes, ping_resp->resp_time);
#endif
		// calculate moving average with fifo buffer
		fifo_put(ping_resp->resp_time);
		ping_response_count = fifo_in_use();
		for (i = 0; i < ping_response_count; i++) {
			fifo_snoop(&reponse_time, i);
			network_response_time_sum += reponse_time * 1000;
		}
		network_average_response_time_ms = network_response_time_sum / ping_response_count / 1000;
	}
}

ICACHE_FLASH_ATTR
void icmp_ping_sent(void *arg, void *pdata) {
#ifdef DEBUG
	printf("ping finish \r\n");
#endif
}

ICACHE_FLASH_ATTR
void icmp_ping(ip_addr_t *ip) {
//	ping_opt = (struct ping_option *)os_zalloc(sizeof(struct ping_option));
	memset(&ping_opt, 0, sizeof(struct ping_option));

	ping_opt.count = 1;			// try to ping how many times
	ping_opt.coarse_time = 1;	// ping interval
	ping_opt.ip = ip->addr;

	ping_regist_recv(&ping_opt, icmp_ping_recv);
	ping_regist_sent(&ping_opt, icmp_ping_sent);

	ping_start(&ping_opt);
}

ICACHE_FLASH_ATTR
void icmp_ping_mqtt_host(void) {
	err_t dns_err;
	ip_addr_t ip;
	
	dns_err = dns_gethostbyname(MQTT_HOST, &ip, dns_found, NULL);
	if (dns_err == ERR_OK) {
#ifdef DEBUG
		printf("dns_gethostbyname() returned ERR_OK\n\r");
#endif	// DEBUG
		icmp_ping(&ip);
	}
	else if (dns_err == ERR_INPROGRESS) {
		/* DNS request sent, wait for dns_found being called */
#ifdef DEBUG
		printf("dns_gethostbyname() returned ERR_INPROGRESS\n\r");
#endif	// DEBUG
	}
}

// fifo
ICACHE_FLASH_ATTR
uint8_t fifo_in_use() {
	return h - t;
}

ICACHE_FLASH_ATTR
bool fifo_put(uint32_t c) {
	if (fifo_in_use() != MOVING_AVERAGE_BUFFER_SIZE) {
		network_moving_average_buffer[h++ % MOVING_AVERAGE_BUFFER_SIZE] = c;
		// wrap
		if (h == MOVING_AVERAGE_BUFFER_SIZE) {
			h = 0;
		}
		return true;
	}
	else {
		return false;
	}
}

/*
ICACHE_FLASH_ATTR
bool fifo_get(uint32_t *c) {
	if (fifo_in_use() != 0) {
		*c = network_moving_average_buffer[t++ % MOVING_AVERAGE_BUFFER_SIZE];
		// wrap
		if (t == MOVING_AVERAGE_BUFFER_SIZE) {
			t = 0;
		}
		return true;
	}
	else {
		return false;
	}
}
*/

ICACHE_FLASH_ATTR
bool fifo_snoop(uint32_t *c, uint8_t pos) {
	if (fifo_in_use() > (pos)) {
        *c = network_moving_average_buffer[(t + pos) % MOVING_AVERAGE_BUFFER_SIZE];
		return true;
	}
	else {
		return false;
	}
}

