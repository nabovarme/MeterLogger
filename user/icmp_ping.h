#define NETWORK_AVERAGE_RESPONSE_TIME_MS_LENGTH 8

extern float ping_average_response_time_ms;
extern float ping_average_packet_loss;

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

typedef struct {
	uint32_t *buffer;
	uint8_t fill_count;
	uint8_t h;
	uint8_t t;
} ring_buffer_t;

ICACHE_FLASH_ATTR
bool ring_buffer_put(ring_buffer_t *rb, uint32_t c);

ICACHE_FLASH_ATTR
bool ring_buffer_remove_last(ring_buffer_t *rb);

ICACHE_FLASH_ATTR
bool ring_buffer_snoop(ring_buffer_t *rb, uint32_t *c, uint8_t pos);


ICACHE_FLASH_ATTR
void init_icmp_ping(void);
