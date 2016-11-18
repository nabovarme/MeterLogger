#define ICACHE_FLASH_ATTR
#define LOCAL static
#define BOOL bool
#define TRUE true
#define FALSE false

#ifndef NULL
#define NULL (void *)0
#endif

#define os_memset memset
#define os_printf printf
#define os_strcpy strcpy
#define os_strlen strlen
#define os_zalloc malloc

#define PRINT_QUEUE
