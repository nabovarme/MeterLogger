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

struct ping_option ping_opt;

void ICACHE_FLASH_ATTR
user_ping_recv(void *arg, void *pdata) {
	struct ping_resp *ping_resp = pdata;
//	struct ping_option *ping_opt = arg;

	if (ping_resp->ping_err == -1) {
#ifdef DEBUG
		printf("ping host fail \r\n");
#endif
	}
	else {
#ifdef DEBUG
		printf("ping recv: byte = %d, time = %d ms \r\n",ping_resp->bytes,ping_resp->resp_time);
#endif
	}
}

void ICACHE_FLASH_ATTR
user_ping_sent(void *arg, void *pdata) {
#ifdef DEBUG
	printf("user ping finish \r\n");
#endif
}


void ICACHE_FLASH_ATTR
user_test_ping(void) {
	const char* ping_ip = "8.8.8.8";

//	ping_opt = (struct ping_option *)os_zalloc(sizeof(struct ping_option));
	memset(&ping_opt, 0, sizeof(struct ping_option));

	ping_opt.count = 10;	//  try to ping how many times
	ping_opt.coarse_time = 2;  // ping interval
	ping_opt.ip = ipaddr_addr(ping_ip);

	ping_regist_recv(&ping_opt, user_ping_recv);
	ping_regist_sent(&ping_opt, user_ping_sent);

	ping_start(&ping_opt);

}
