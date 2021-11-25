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
uint8_t fifo_in_use();

ICACHE_FLASH_ATTR
bool fifo_put(uint32_t c);

/*
ICACHE_FLASH_ATTR
bool fifo_get(uint32_t *c);
*/

ICACHE_FLASH_ATTR
bool fifo_snoop(uint32_t *c, uint8_t pos);
