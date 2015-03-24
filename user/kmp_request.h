
#define at_recvTaskPrio        0
#define at_recvTaskQueueLen    64

os_event_t    at_recvTaskQueue[at_recvTaskQueueLen];

ICACHE_FLASH_ATTR
static void at_recvTask(os_event_t *events);


ICACHE_FLASH_ATTR
void kmp_request_init();

ICACHE_FLASH_ATTR
void kmp_request_send();

// fifo
ICACHE_FLASH_ATTR
unsigned int kmp_fifo_in_use();

ICACHE_FLASH_ATTR
unsigned char fifo_put(unsigned char c);

ICACHE_FLASH_ATTR
unsigned char kmp_fifo_get(unsigned char *c);

ICACHE_FLASH_ATTR
unsigned char kmp_fifo_snoop(unsigned char *c, unsigned int pos);
