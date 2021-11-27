#define NETWORK_AVERAGE_RESPONSE_TIME_MS_LENGTH 8

extern float network_average_response_time_ms;
extern uint32_t network_response_time_error_count;

ICACHE_FLASH_ATTR
void dns_found(const char *name, ip_addr_t *ip, void *arg);

ICACHE_FLASH_ATTR
void icmp_ping_recv(void *arg, void *pdata);

ICACHE_FLASH_ATTR
void icmp_ping_sent(void *arg, void *pdata);

ICACHE_FLASH_ATTR
void icmp_ping(ip_addr_t *ip);

ICACHE_FLASH_ATTR
void icmp_ping_mqtt_host(void);

// fifo
ICACHE_FLASH_ATTR
bool fifo_put(uint32_t c);

ICACHE_FLASH_ATTR
bool fifo_remove_last();

ICACHE_FLASH_ATTR
bool fifo_snoop(uint32_t *c, uint8_t pos);

ICACHE_FLASH_ATTR
void init_icmp_ping(void);
