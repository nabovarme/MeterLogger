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
struct espconn *pCon;


unsigned long ICACHE_FLASH_ATTR ntp_get_time()
{
  pCon = (struct espconn *)os_zalloc(sizeof(struct espconn));
  pCon->type = ESPCONN_UDP;
  pCon->state = ESPCONN_NONE;
  pCon->proto.udp = (esp_udp *)os_zalloc(sizeof(esp_udp));
  pCon->proto.udp->local_port = espconn_port();
  pCon->proto.udp->remote_port = 123;
  ntp_send_request();
}

void ICACHE_FLASH_ATTR
ntp_udpclient_sent_cb(void *arg)
{
  //Causing Crash. To be fixed
  //espconn_delete(pCon);
  //os_free(pCon->proto.udp);
  //os_free(pCon);
}

void ICACHE_FLASH_ATTR
ntp_udpclient_recv(void *arg, char *pdata, unsigned short len)
{
  struct tm *dt;
  char timestr[11];
  // this is NTP time (seconds since Jan 1 1900):
  uint32 timestamp = pdata[40] << 24 | pdata[41] << 16 |
    pdata[42] << 8 | pdata[43];				// BUG? is ntp 32 or 64 bit?
  timestamp =  timestamp - NTP_OFFSET;
  //INFO("timestamp: %lu\n\r", timestamp);
  set_unix_time(timestamp);
//  dt = localtime((time_t *) &timestamp);
//  os_sprintf(timestr, "%d:%d:%d\n\r", dt->tm_hour, dt->tm_min, dt->tm_sec);
//  INFO(timestr);
}

void ICACHE_FLASH_ATTR ntp_send_request() {
	ip_addr_t ip;	// not used, callback are handling ip
	espconn_gethostbyname(pCon, NTP_SERVER, &ip, ntp_dns_found);
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
    os_memcpy(pCon->proto.udp->remote_ip, &ipaddr->addr, 4);
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
    espconn_create(pCon);
    espconn_regist_recvcb(pCon, ntp_udpclient_recv);
    espconn_regist_sentcb(pCon, ntp_udpclient_sent_cb);
    espconn_sent(pCon, packetBuffer, NTP_PACKET_SIZE);
}
