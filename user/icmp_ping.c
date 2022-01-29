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

#include "tinyprintf.h"
#include "utils.h"
#include "icmp_ping.h"

#define MOVING_AVERAGE_BUFFER_SIZE 60

uint32_t ping_count = 0;

float ping_average_response_time_ms = 0.0;
float ping_average_packet_loss = 0.0;

ring_buffer_t ping_response_time_ring_buffer;
uint32_t response_time_buffer[MOVING_AVERAGE_BUFFER_SIZE];	// 1 hour moving average

ring_buffer_t ping_packet_loss_ring_buffer;
uint32_t packet_loss_buffer[MOVING_AVERAGE_BUFFER_SIZE];	// 1 hour moving average

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
	uint32_t data = 0;
	uint32_t ping_response_time_sum = 0;
	uint32_t ping_packet_loss_sum = 0;
	uint8_t i;

	if (ping_resp->ping_err == -1) {
#ifdef DEBUG
		printf("ping host fail \r\n");
#endif
		// count packets lost
		if (ping_packet_loss_ring_buffer.fill_count == MOVING_AVERAGE_BUFFER_SIZE) {
			ring_buffer_remove_last(&ping_packet_loss_ring_buffer);
		}
		ring_buffer_put(&ping_packet_loss_ring_buffer, 1);
	}
	else {
#ifdef DEBUG
		printf("ping recv: byte = %d, time = %d ms \r\n", ping_resp->bytes, ping_resp->resp_time);
#endif
		// count packets received
		if (ping_packet_loss_ring_buffer.fill_count == MOVING_AVERAGE_BUFFER_SIZE) {
			ring_buffer_remove_last(&ping_packet_loss_ring_buffer);
		}
		ring_buffer_put(&ping_packet_loss_ring_buffer, 0);
		
		// add to ping_response_time_ring_buffer
		if (ping_response_time_ring_buffer.fill_count == MOVING_AVERAGE_BUFFER_SIZE) {
			ring_buffer_remove_last(&ping_response_time_ring_buffer);
		}
		ring_buffer_put(&ping_response_time_ring_buffer, ping_resp->resp_time);
		
		// calculate moving average with ring buffer for ping_response_time
		if (ping_response_time_ring_buffer.fill_count > 0) {	// just to be sure
			for (i = 0; i < ping_response_time_ring_buffer.fill_count; i++) {
				ring_buffer_snoop(&ping_response_time_ring_buffer, &data, i);
				ping_response_time_sum += data;
			}
			ping_average_response_time_ms = (float)ping_response_time_sum / (float)ping_response_time_ring_buffer.fill_count;
		}
	}
		
	// calculate moving average with ring buffer for ping_packet_loss
	if (ping_packet_loss_ring_buffer.fill_count > 0) {	// just to be sure
		for (i = 0; i < ping_packet_loss_ring_buffer.fill_count; i++) {
			ring_buffer_snoop(&ping_packet_loss_ring_buffer, &data, i);
			ping_packet_loss_sum += data;
		}
		ping_average_packet_loss = (float)ping_packet_loss_sum / (float)ping_packet_loss_ring_buffer.fill_count;
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
	
	ping_count++;

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

// ring buffer
ICACHE_FLASH_ATTR
bool ring_buffer_put(ring_buffer_t *rb, uint32_t c) {
	if (rb->fill_count != MOVING_AVERAGE_BUFFER_SIZE) {
		rb->buffer[rb->h++ % MOVING_AVERAGE_BUFFER_SIZE] = c;
		// wrap
		if (rb->h == MOVING_AVERAGE_BUFFER_SIZE) {
			rb->h = 0;
		}
		rb->fill_count++;
		return true;
	}
	else {
		return false;
	}
}

ICACHE_FLASH_ATTR
bool ring_buffer_remove_last(ring_buffer_t *rb) {
	if (rb->fill_count != 0) {
		rb->t++;
		// wrap
		if (rb->t == MOVING_AVERAGE_BUFFER_SIZE) {
			rb->t = 0;
		}
		rb->fill_count--;
		return true;
	}
	else {
		return false;
	}
}

ICACHE_FLASH_ATTR
bool ring_buffer_snoop(ring_buffer_t *rb, uint32_t *c, uint8_t pos) {
	if (rb->fill_count > (pos)) {
		*c = rb->buffer[(rb->t + pos) % MOVING_AVERAGE_BUFFER_SIZE];
		return true;
	}
	else {
		return false;
	}
}

ICACHE_FLASH_ATTR
void init_icmp_ping(void) {
	// init ring buffers
	ping_response_time_ring_buffer.fill_count = 0;
	ping_response_time_ring_buffer.h = 0;
	ping_response_time_ring_buffer.t = 0;
	ping_response_time_ring_buffer.buffer = (uint32_t *)&response_time_buffer;

	ping_packet_loss_ring_buffer.fill_count = 0;
	ping_packet_loss_ring_buffer.h = 0;
	ping_packet_loss_ring_buffer.t = 0;
	ping_packet_loss_ring_buffer.buffer = (uint32_t *)&packet_loss_buffer;
}
