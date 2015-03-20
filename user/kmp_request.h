
#define at_recvTaskPrio        0
#define at_recvTaskQueueLen    64

os_event_t    at_recvTaskQueue[at_recvTaskQueueLen];

ICACHE_FLASH_ATTR
static void at_recvTask(os_event_t *events);


ICACHE_FLASH_ATTR
void kmp_request_init();

ICACHE_FLASH_ATTR
void kmp_request_send();

