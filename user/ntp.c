#include "c_types.h"
#include <ip_addr.h>
#include "osapi.h"
#include "mem.h"
#include "espconn.h"
#include "debug.h"
#include "time.h"
#include "unix_time.h"
#include "ntp.h"

uint8 ntp_server[4];
uint8 packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
struct espconn conn;
esp_udp udp_proto;


unsigned long ICACHE_FLASH_ATTR ntp_get_time() {
	// create new connection
	memset(&conn, 0x00, sizeof(struct espconn));
	conn.type = ESPCONN_UDP;
	conn.state = ESPCONN_NONE;
	memset(&udp_proto, 0x00, sizeof(esp_udp));
	conn.proto.udp = &udp_proto;
	conn.proto.udp->local_port = espconn_port();
	conn.proto.udp->remote_port = 123;
	ntp_send_request();
}

void ICACHE_FLASH_ATTR
ntp_udpclient_recv(void *arg, char *pdata, unsigned short len) {
	struct tm *dt;
	char timestr[11];
	// this is NTP time (seconds since Jan 1 1900):
	uint32 timestamp = pdata[40] << 24 | pdata[41] << 16 |
		pdata[42] << 8 | pdata[43];				// BUG? is ntp 32 or 64 bit?
	timestamp =  timestamp - NTP_OFFSET;
	set_unix_time(timestamp);
}

void ICACHE_FLASH_ATTR ntp_send_request() {
	ip_addr_t ip;	// not used, callback are handling ip
	espconn_gethostbyname(&conn, NTP_SERVER, &ip, ntp_dns_found);
}

void ICACHE_FLASH_ATTR ntp_dns_found(const char *name, ip_addr_t *ipaddr, void *arg) {
	if (ipaddr == NULL) {
		INFO("DNS: Found, but got no ip, try to reconnect\r\n");
		return;
	}

	INFO("DNS: found ip %d.%d.%d.%d\n",
			*((uint8 *) &ipaddr->addr),
			*((uint8 *) &ipaddr->addr + 1),
			*((uint8 *) &ipaddr->addr + 2),
			*((uint8 *) &ipaddr->addr + 3));
	
	// send ntp request
    // set all bytes in the buffer to 0
    os_memcpy(conn.proto.udp->remote_ip, &ipaddr->addr, 4);
    memset(packetBuffer, 0, NTP_PACKET_SIZE);
    // Initialize values needed to form NTP request
    // (see URL above for details on the packets)
    packetBuffer[0] = 0b11100011;   // LI, Version, Mode
    packetBuffer[1] = 0;     // Stratum, or type of clock
    packetBuffer[2] = 6;     // Polling Interval
    packetBuffer[3] = 0xEC;  // Peer Clock Precision
    // 8 bytes of zero for Root Delay & Root Dispersion
    packetBuffer[12]  = 49;
    packetBuffer[13]  = 0x4E;
    packetBuffer[14]  = 49;
    packetBuffer[15]  = 52;

    // all NTP fields have been given values, now
    // you can send a packet requesting a timestamp:
    espconn_create(&conn);
    espconn_regist_recvcb(&conn, ntp_udpclient_recv);
    espconn_sent(&conn, packetBuffer, NTP_PACKET_SIZE);
}
