#define RTC_ADDR 64  // Word offset (64*4 = 256 bytes into RTC memory)
#define MAGIC 0xDEADBEEF

// A struct to hold your data (must be 4-byte aligned)
typedef struct {
	uint32_t magic;	 // To verify validity
	uint32_t data;
} rtc_data_t;

ICACHE_FLASH_ATTR void save_rtc_data(uint32_t data);
ICACHE_FLASH_ATTR bool load_rtc_data(uint32 *data);
