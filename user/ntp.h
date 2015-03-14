#ifndef NTP_H
#define NTP_H

#define NTP_PACKET_SIZE 48 // NTP time stamp is in the first 48 bytes of the message
#define NTP_OFFSET   2208988800UL

#define NTP_SERVER "dk.pool.ntp.org"

extern uint64 unix_time;
extern bool unix_time_mutex;

unsigned long ICACHE_FLASH_ATTR ntp_get_time();
void ICACHE_FLASH_ATTR ntp_send_request();
void ICACHE_FLASH_ATTR ntp_udpclient_recv(void *arg, char *pdata, unsigned short len);
void ICACHE_FLASH_ATTR ntp_dns_found(const char *name, ip_addr_t *ipaddr, void *arg);

#endif
