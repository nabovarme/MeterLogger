#ifndef NTP_H
#define NTP_H

#define NTP_PACKET_SIZE 48 // NTP time stamp is in the first 48 bytes of the message
#define NTP_OFFSET   2208988800UL

extern uint64 unix_time;
extern bool unix_time_mutex;

unsigned long ntp_get_time();
void ntp_send_request();
void ntp_udpclient_recv(void *arg, char *pdata, unsigned short len);

#endif
