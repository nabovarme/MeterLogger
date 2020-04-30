/*
 * lwip_netstat.c
 *
 *  Created on: Apr 30, 2020
 *      Author: st0ff3r
 */
#include <esp8266.h>
#include "lwip/netif.h"
#include "lwip/app/espconn_tcp.h"

#include "tinyprintf.h"


void ICACHE_FLASH_ATTR lwip_netstat() {
	struct netif *netif = netif_list;
	extern espconn_msg *plink_active;
//	extern espconn_msg *pserver_list;
//	extern struct espconn_packet pktinfo[2];
//	extern struct tcp_pcb ** const tcp_pcb_lists[];

	espconn_msg *plist = NULL;

	/* loop through netif's */
	while (netif != NULL) {
		/* proceed to next network interface */
#ifdef DEBUG
		printf("netif name: %c%c%u\n\r", netif->name[0], netif->name[1], netif->num);
#endif
		netif = netif->next;
	}
	
	
    // find active connection nodes
    for (plist = plink_active; plist != NULL; plist = plist->pnext){
		if (plist->pespconn->type == ESPCONN_TCP) {
#ifdef DEBUG
			printf("plink_active type: ESPCONN_TCP, local: " IPSTR ":%u, remote: " IPSTR ":%u\n\r",
				IP2STR(&plist->pespconn->proto.tcp->local_ip),
				plist->pespconn->proto.tcp->local_port,
				IP2STR(&plist->pespconn->proto.tcp->remote_ip),
				plist->pespconn->proto.tcp->remote_port
			);
#endif
		}
		else if (plist->pespconn->type == ESPCONN_UDP) {
#ifdef DEBUG
			printf("plink_active type: ESPCONN_UDP, local: " IPSTR ":%u, remote: " IPSTR ":%u\n\r",
				IP2STR(&plist->pespconn->proto.udp->local_ip),
				plist->pespconn->proto.udp->local_port,
				IP2STR(&plist->pespconn->proto.udp->remote_ip),
				plist->pespconn->proto.udp->remote_port
			);
#endif
		}
		else {
			// ESPCONN_INVALID
		}
	}
	
	
}
