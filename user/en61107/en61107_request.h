#ifndef EN61107_REQUEST_H
#define EN61107_REQUEST_H

#include "mqtt.h"
#include "en61107.h"

#define en61107_received_task_prio			USER_TASK_PRIO_2
#define en61107_received_task_queue_length	64

typedef void (*meter_is_ready_cb)(void);
typedef void (*meter_sent_data_cb)(void);

os_event_t    en61107_received_task_queue[en61107_received_task_queue_length];

ICACHE_FLASH_ATTR
void en61107_request_init();

ICACHE_FLASH_ATTR
void en61107_set_mqtt_client(MQTT_Client* client);

ICACHE_FLASH_ATTR
void en61107_register_meter_is_ready_cb(meter_is_ready_cb cb);

ICACHE_FLASH_ATTR
void en61107_register_meter_sent_data_cb(meter_sent_data_cb cb);

ICACHE_FLASH_ATTR
unsigned int en61107_get_received_serial();

#ifdef FLOW_METER
ICACHE_FLASH_ATTR
unsigned int en61107_get_received_volume_l();
#else
ICACHE_FLASH_ATTR
unsigned int en61107_get_received_energy_kwh();
#endif	// FLOW_METER

//ICACHE_FLASH_ATTR
inline bool en61107_is_eod_char(uint8_t c);

ICACHE_FLASH_ATTR
void en61107_request_send();

// uart send en61107 commands
ICACHE_FLASH_ATTR
void en61107_uart_send_en61107_ident();

ICACHE_FLASH_ATTR
void en61107_uart_send_en61107();

ICACHE_FLASH_ATTR
void en61107_uart_send_standard_data_1();

ICACHE_FLASH_ATTR
void en61107_uart_send_standard_data_2();

#ifndef MC_66B
ICACHE_FLASH_ATTR
void en61107_uart_send_inst_values();
#endif


// fifo
ICACHE_FLASH_ATTR
unsigned int en61107_fifo_in_use();

//ICACHE_FLASH_ATTR
inline unsigned char en61107_fifo_put(unsigned char c);

ICACHE_FLASH_ATTR
unsigned char en61107_fifo_get(unsigned char *c);

ICACHE_FLASH_ATTR
unsigned char en61107_fifo_snoop(unsigned char *c, unsigned int pos);

ICACHE_FLASH_ATTR
void en61107_request_destroy();

// private methods

ICACHE_FLASH_ATTR
void en61107_receive_timeout_timer_func(void *arg);

ICACHE_FLASH_ATTR
void en61107_delayed_uart_change_setting_timer_func(UartDevice *uart_settings);

ICACHE_FLASH_ATTR
void en61107_meter_wake_up_timer_func(void *arg);

#endif

