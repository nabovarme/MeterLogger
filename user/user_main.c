#include <esp8266.h>
#include "driver/uart.h"
#include "mqtt.h"
#include "wifi.h"
#include "config.h"
#include "debug.h"
#include "httpd_user_init.h"
#include "user_config.h"
#include "unix_time.h"
#include "user_main.h"
#include "kmp_request.h"
#include "en61107_request.h"
#include "cron.h"
#include "led.h"
#include "ac_out.h"

#ifdef IMPULSE
char impulse_meter_serial[IMPULSE_METER_SERIAL_LEN];
uint32_t impulse_meter_energy;
uint32_t impulses_per_kwh;

volatile uint32_t impulse_time;
volatile uint32_t last_impulse_time;
volatile uint32_t current_energy;	// in W

volatile uint32_t last_impulse_meter_count;

uint32_t impulse_falling_edge_time;
uint32_t impulse_rising_edge_time;
#ifdef POWER_WD
// for power wd
bool shutdown = false;
#endif // POWER_WD
#endif // ENDIF IMPULSE

MQTT_Client mqttClient;
static os_timer_t sample_timer;
static os_timer_t config_mode_timer;
static os_timer_t sample_mode_timer;
static os_timer_t en61107_request_send_timer;
#ifdef EN61107
static os_timer_t en61107_request_send_timer;
#elif defined IMPULSE
static os_timer_t kmp_request_send_timer;
#else
static os_timer_t kmp_request_send_timer;
#endif

#ifdef IMPULSE
static os_timer_t impulse_meter_calculate_timer;
#ifdef POWER_WD
static os_timer_t power_wd_timer;
uint16_t vdd_init;
#endif // POWER_WD
#endif

uint16 counter = 0;

struct rst_info *rtc_info;

ICACHE_FLASH_ATTR void sample_mode_timer_func(void *arg) {
	unsigned char topic[128];
	int topic_l;
	uint32_t impulse_meter_count_temp;
	
	// stop http configuration server
	httpdStop();

	// save sys_cfg.impulse_meter_count - in case it has been incremented since cfg_load() at boot
	impulse_meter_count_temp = sys_cfg.impulse_meter_count;
	// reload save configuration
	cfg_load();
	// ...and restore sys_cfg.impulse_meter_count
	sys_cfg.impulse_meter_count = impulse_meter_count_temp;
	
	MQTT_InitConnection(&mqttClient, sys_cfg.mqtt_host, sys_cfg.mqtt_port, sys_cfg.security);

	MQTT_InitClient(&mqttClient, sys_cfg.device_id, sys_cfg.mqtt_user, sys_cfg.mqtt_pass, sys_cfg.mqtt_keepalive, 1);

	// set MQTT LWP topic
#ifdef IMPULSE
	topic_l = os_sprintf(topic, "/offline/v1/%s", impulse_meter_serial);
#else
	topic_l = os_sprintf(topic, "/offline/v1/%07u", kmp_get_received_serial());
#endif
	MQTT_InitLWT(&mqttClient, topic, "", 0, 0);
	
	MQTT_OnConnected(&mqttClient, mqttConnectedCb);
	MQTT_OnDisconnected(&mqttClient, mqttDisconnectedCb);
	MQTT_OnPublished(&mqttClient, mqttPublishedCb);
	MQTT_OnData(&mqttClient, mqttDataCb);

	wifi_connect(sys_cfg.sta_ssid, sys_cfg.sta_pwd, wifi_changed_cb);
}

ICACHE_FLASH_ATTR void config_mode_timer_func(void *arg) {
	struct softap_config ap_conf;
	
	INFO("\r\nAP mode\r\n");
	
	wifi_softap_get_config(&ap_conf);
	os_memset(ap_conf.ssid, 0, sizeof(ap_conf.ssid));
	os_memset(ap_conf.password, 0, sizeof(ap_conf.password));
#ifdef IMPULSE
	os_sprintf(ap_conf.ssid, AP_SSID, impulse_meter_serial);
#else
	os_sprintf(ap_conf.ssid, AP_SSID, kmp_get_received_serial());
#endif
	os_sprintf(ap_conf.password, AP_PASSWORD);
	ap_conf.authmode = STA_TYPE;
	ap_conf.ssid_len = 0;
	ap_conf.beacon_interval = 100;
	ap_conf.channel = 1;
	ap_conf.max_connection = 4;
	ap_conf.ssid_hidden = 0;

	wifi_softap_set_config_current(&ap_conf);

	httpd_user_init();
}

ICACHE_FLASH_ATTR void sample_timer_func(void *arg) {
#ifdef EN61107
	en61107_request_send();
#elif defined IMPULSE
	char mqtt_topic[128];
	char mqtt_message[128];
	int mqtt_topic_l;
	int mqtt_message_l;
	
	uint32_t acc_energy;
	
	// for pseudo float print
	char current_energy_kwh[32];
	char acc_energy_kwh[32];
	
    uint32_t result_int, result_frac;
	int8_t exponent;
	unsigned char leading_zeroes[16];
	unsigned int i;

	if (impulse_time > (uptime() - 60)) {	// only send mqtt if impulse received last minute
		acc_energy = (impulse_meter_energy * 1000) + (sys_cfg.impulse_meter_count * (1000 / impulses_per_kwh));
	
	    // for acc_energy...
	    // ...divide by 1000 and prepare decimal string in kWh
	    result_int = (int32_t)(acc_energy / 1000);
	    result_frac = acc_energy - result_int * 1000;
    
	    // prepare decimal string
	    strcpy(leading_zeroes, "");
	    for (i = 0; i < (3 - impulse_meter_decimal_number_length(result_frac)); i++) {
	        strcat(leading_zeroes, "0");
	    }
	    sprintf(acc_energy_kwh, "%u.%s%u", result_int, leading_zeroes, result_frac);

    	// for current_energy...
    	// ...divide by 1000 and prepare decimal string in kWh
    	result_int = (int32_t)(current_energy / 1000);
    	result_frac = current_energy - result_int * 1000;
    	
    	// prepare decimal string
    	strcpy(leading_zeroes, "");
    	for (i = 0; i < (3 - impulse_meter_decimal_number_length(result_frac)); i++) {
    	    strcat(leading_zeroes, "0");
    	}
    	os_sprintf(current_energy_kwh, "%u.%s%u", result_int, leading_zeroes, result_frac);
    	
		mqtt_topic_l = os_sprintf(mqtt_topic, "/sample/v1/%s/%u", impulse_meter_serial, get_unix_time());
		mqtt_message_l = os_sprintf(mqtt_message, "heap=%lu&effect1=%s kW&e1=%s kWh&", system_get_free_heap_size(), current_energy_kwh, acc_energy_kwh);

		if (&mqttClient) {
			// if mqtt_client is initialized
			MQTT_Publish(&mqttClient, mqtt_topic, mqtt_message, mqtt_message_l, 0, 0);
		}

		// set offset for next calculation
		last_impulse_meter_count = sys_cfg.impulse_meter_count;
		last_impulse_time = impulse_time;
#ifdef DEBUG
		os_printf("current_energy: %u\n", current_energy);
#endif
	}
	else {
		// send ping to keep mqtt alive
		if (&mqttClient) {
			// if mqtt_client is initialized
			MQTT_Ping(&mqttClient);
		}
	}

#else
	kmp_request_send();
#endif
}

ICACHE_FLASH_ATTR void kmp_request_send_timer_func(void *arg) {
	kmp_request_send();
}

ICACHE_FLASH_ATTR void en61107_request_send_timer_func(void *arg) {
	en61107_request_send();
}

#ifdef IMPULSE
ICACHE_FLASH_ATTR void impulse_meter_calculate_timer_func(void *arg) {
	uint32_t impulse_time_diff;
	uint32_t impulse_meter_count_diff;

	impulse_time = uptime();
	impulse_time_diff = impulse_time - last_impulse_time;
	
	impulse_meter_count_diff = sys_cfg.impulse_meter_count - last_impulse_meter_count;
#ifdef DEBUG
	os_printf("count: %u\tl count: %u\timp time: %u\tlast imp time: %u\n", sys_cfg.impulse_meter_count, last_impulse_meter_count, impulse_time, last_impulse_time);
	os_printf("count diff: %u\timp time diff: %u\n", impulse_meter_count_diff, impulse_time_diff);
#endif

	if (impulse_time_diff && impulse_meter_count_diff) {	// only calculate if not zero interval or zero meter count diff - should not happen
		current_energy = 3600 * (1000 / impulses_per_kwh) * impulse_meter_count_diff / impulse_time_diff;
	}

#ifdef DEBUG
	os_printf("current_energy: %u\n", current_energy);
#endif // DEBUG
}
#endif // IMPULSE

#ifdef IMPULSE
#ifdef POWER_WD
ICACHE_FLASH_ATTR void power_wd_timer_func(void *arg) {
	uint16_t vdd;
	vdd = system_get_vdd33();
	if ((vdd < (vdd_init - 100)) && (shutdown == false)) {
		cfg_save();
//		os_printf("\n\rvdd: %d\n\r", vdd);
		if (&mqttClient) {
			// if mqtt_client is initialized
			shutdown = true;
			MQTT_Publish(&mqttClient, "/shutdown", "", 1, 0, 0);	// DEBUG: needs serial
		}
	}
}
#endif // POWER_WD
#endif // IMPULSE

ICACHE_FLASH_ATTR void wifi_changed_cb(uint8_t status) {
	if (status == STATION_GOT_IP) {
		MQTT_Connect(&mqttClient);
	}
}

ICACHE_FLASH_ATTR void mqttConnectedCb(uint32_t *args) {
	MQTT_Client* client = (MQTT_Client*)args;
	unsigned char topic[128];
	int topic_l;

#ifdef DEBUG
	os_printf("\n\rMQTT: Connected\n\r");
#endif

	// set MQTT LWP topic and subscribe to /config/v1/serial/#
#ifdef IMPULSE
	topic_l = os_sprintf(topic, "/config/v1/%s/#", impulse_meter_serial);
#else
	topic_l = os_sprintf(topic, "/config/v1/%07u/#", kmp_get_received_serial());
#endif
	MQTT_Subscribe(client, topic, 0);

	// set mqtt_client kmp_request should use to return data
#ifdef EN61107
	en61107_set_mqtt_client(client);
#elif defined IMPULSE
	//kmp_set_mqtt_client(client);
#else
	kmp_set_mqtt_client(client);
#endif
	
	// sample once and start sample timer
	sample_timer_func(NULL);
    os_timer_disarm(&sample_timer);
    os_timer_setfn(&sample_timer, (os_timer_func_t *)sample_timer_func, NULL);
    os_timer_arm(&sample_timer, 60000, 1);		// every 60 seconds
}

ICACHE_FLASH_ATTR void mqttDisconnectedCb(uint32_t *args) {
	MQTT_Client* client = (MQTT_Client*)args;
	INFO("MQTT: Disconnected - reconnect\r\n");
	MQTT_Connect(&mqttClient);
}

ICACHE_FLASH_ATTR void mqttPublishedCb(uint32_t *args) {
	MQTT_Client* client = (MQTT_Client*)args;
	INFO("MQTT: Published\r\n");
}

ICACHE_FLASH_ATTR void mqttDataCb(uint32_t *args, const char* topic, uint32_t topic_len, const char *data, uint32_t data_len) {
	char *topicBuf = (char*)os_zalloc(topic_len + 1);	// DEBUG: could we avoid malloc here?
	char *dataBuf = (char*)os_zalloc(data_len + 1);
	MQTT_Client* client = (MQTT_Client*)args;
	
	char *str;
	char function_name[FUNCTIONNAME_L];

	unsigned char reply_topic[128];
	unsigned char reply_message[8];
	int reply_topic_l;
	int reply_message_l;

	os_memcpy(topicBuf, topic, topic_len);
	topicBuf[topic_len] = 0;

	os_memcpy(dataBuf, data, data_len);
	dataBuf[data_len] = 0;

	// parse mqtt topic for function call name
	str = strtok(topicBuf, "/");
	while (str != NULL) {
		strncpy(function_name, str, FUNCTIONNAME_L);   // save last parameter as function_name
		str = strtok(NULL, "/");
	}
	
	// mqtt rpc dispatcher goes here
	if (strncmp(function_name, "set_cron", FUNCTIONNAME_L) == 0) {
		// found set_cron
		add_cron_job_from_query(dataBuf);
	}
	else if (strncmp(function_name, "clear_cron", FUNCTIONNAME_L) == 0) {
		// found clear_cron
		clear_cron_jobs();
	}
	else if (strncmp(function_name, "cron", FUNCTIONNAME_L) == 0) {
		// found cron
#ifdef IMPULSE
		reply_topic_l = os_sprintf(reply_topic, "/cron/v1/%s/%u", impulse_meter_serial, get_unix_time());
#else
		reply_topic_l = os_sprintf(reply_topic, "/cron/v1/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
		reply_message_l = os_sprintf(reply_message, "%d", sys_cfg.cron_jobs.n);

		if (&mqttClient) {
			// if mqtt_client is initialized
			MQTT_Publish(&mqttClient, reply_topic, reply_message, reply_message_l, 0, 0);
		}
	}
	else if (strncmp(function_name, "open", FUNCTIONNAME_L) == 0) {
		// found open
		//ac_motor_valve_open();
		sys_cfg.ac_thermo_state = 1;
		ac_thermo_open();
	}
	else if (strncmp(function_name, "close", FUNCTIONNAME_L) == 0) {
		// found close
		//ac_motor_valve_close();
		sys_cfg.ac_thermo_state = 0;
		ac_thermo_close();
	}
	else if (strncmp(function_name, "status", FUNCTIONNAME_L) == 0) {
		// found status
#ifdef IMPULSE
		reply_topic_l = os_sprintf(reply_topic, "/status/v1/%s/%u", impulse_meter_serial, get_unix_time());
#else
		reply_topic_l = os_sprintf(reply_topic, "/status/v1/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
		reply_message_l = os_sprintf(reply_message, "%s", sys_cfg.ac_thermo_state ? "open" : "close");

		if (&mqttClient) {
			// if mqtt_client is initialized
			MQTT_Publish(&mqttClient, reply_topic, reply_message, reply_message_l, 0, 0);
		}
	}
	else if (strncmp(function_name, "off", FUNCTIONNAME_L) == 0) {
		// found off
		// turn ac output off
		ac_off();
	}
	else if (strncmp(function_name, "pwm", FUNCTIONNAME_L) == 0) {
		// found pwm
		// start ac 1 pwm
		ac_thermo_pwm(atoi(dataBuf));
	}
	else if (strncmp(function_name, "test", FUNCTIONNAME_L) == 0) {
		// found test
		ac_test();
	}
	else if (strncmp(function_name, "ping", FUNCTIONNAME_L) == 0) {
		// found ping
#ifdef IMPULSE
		reply_topic_l = os_sprintf(reply_topic, "/ping/v1/%s/%u", impulse_meter_serial, get_unix_time());
#else
		reply_topic_l = os_sprintf(reply_topic, "/ping/v1/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
		reply_message_l = os_sprintf(reply_message, "");

		if (&mqttClient) {
			// if mqtt_client is initialized
			MQTT_Publish(&mqttClient, reply_topic, reply_message, reply_message_l, 0, 0);
		}
	}
	else if (strncmp(function_name, "version", FUNCTIONNAME_L) == 0) {
		// found version
#ifdef IMPULSE
		reply_topic_l = os_sprintf(reply_topic, "/version/v1/%s/%u", impulse_meter_serial, get_unix_time());
#else
		reply_topic_l = os_sprintf(reply_topic, "/version/v1/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
		reply_message_l = os_sprintf(reply_message, "%s-%s", system_get_sdk_version(), VERSION);

		if (&mqttClient) {
			// if mqtt_client is initialized
			MQTT_Publish(&mqttClient, reply_topic, reply_message, reply_message_l, 0, 0);
		}
	}
	else if (strncmp(function_name, "uptime", FUNCTIONNAME_L) == 0) {
		// found uptime
#ifdef IMPULSE
		reply_topic_l = os_sprintf(reply_topic, "/uptime/v1/%s/%u", impulse_meter_serial, get_unix_time());
#else
		reply_topic_l = os_sprintf(reply_topic, "/uptime/v1/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
		reply_message_l = os_sprintf(reply_message, "%u", uptime());

		if (&mqttClient) {
			// if mqtt_client is initialized
			MQTT_Publish(&mqttClient, reply_topic, reply_message, reply_message_l, 0, 0);
		}
	}
	else if (strncmp(function_name, "mem", FUNCTIONNAME_L) == 0) {
		// found mem
#ifdef IMPULSE
		reply_topic_l = os_sprintf(reply_topic, "/mem/v1/%07u/%u", impulse_meter_serial, get_unix_time());
#else
		reply_topic_l = os_sprintf(reply_topic, "/mem/v1/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
		reply_message_l = os_sprintf(reply_message, "heap=%lu&", system_get_free_heap_size());
#ifdef DEBUG
		system_print_meminfo();
#endif

		if (&mqttClient) {
			// if mqtt_client is initialized
			MQTT_Publish(&mqttClient, reply_topic, reply_message, reply_message_l, 0, 0);
		}
	}
	else if (strncmp(function_name, "reset_reason", FUNCTIONNAME_L) == 0) {
		// found mem
#ifdef IMPULSE
		reply_topic_l = os_sprintf(reply_topic, "/reset_reason/v1/%07u/%u", impulse_meter_serial, get_unix_time());
#else
		reply_topic_l = os_sprintf(reply_topic, "/reset_reason/v1/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
		reply_message_l = os_sprintf(reply_message, "%d", (rtc_info != NULL) ? rtc_info->reason : -1);

		if (&mqttClient) {
			// if mqtt_client is initialized
			MQTT_Publish(&mqttClient, reply_topic, reply_message, reply_message_l, 0, 0);
		}
	}
#ifdef IMPULSE
	else if (strncmp(function_name, "save", FUNCTIONNAME_L) == 0) {
		// found save - save conf to flash
		cfg_save();
		
		reply_topic_l = os_sprintf(reply_topic, "/save/v1/%s/%u", impulse_meter_serial, get_unix_time());
		reply_message_l = os_sprintf(reply_message, "%u", sys_cfg.impulse_meter_count);

		if (&mqttClient) {
			// if mqtt_client is initialized
			MQTT_Publish(&mqttClient, reply_topic, reply_message, reply_message_l, 0, 0);
		}
	}
#endif
	
	os_free(topicBuf);
	os_free(dataBuf);
}

#ifdef IMPULSE
ICACHE_FLASH_ATTR void gpio_int_init() {
	ETS_GPIO_INTR_DISABLE();										// Disable gpio interrupts
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDI_U, FUNC_GPIO0);				// Set GPIO0 function
	gpio_output_set(0, 0, 0, GPIO_ID_PIN(0));						// Set GPIO0 as input
	//ETS_GPIO_INTR_ATTACH(gpio_int_handler, 0);					// GPIO0 interrupt handler
	gpio_intr_handler_register(gpio_int_handler, NULL);
	PIN_PULLUP_EN(PERIPHS_IO_MUX_GPIO0_U);							// pull - up pin
	GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, BIT(0));				// Clear GPIO0 status
	gpio_pin_intr_state_set(GPIO_ID_PIN(0), GPIO_PIN_INTR_ANYEDGE);	// Interrupt on falling GPIO0 edge
	ETS_GPIO_INTR_ENABLE();											// Enable gpio interrupts
	//wdt_feed();
}
#endif

#ifdef IMPULSE
void gpio_int_handler(uint32_t interrupt_mask, void *arg) {
	uint32_t gpio_status;
	bool impulse_pin_state;
	
	uint32_t impulse_edge_to_edge_time;

	gpio_intr_ack(interrupt_mask);

	ETS_GPIO_INTR_DISABLE(); // Disable gpio interrupts
	gpio_pin_intr_state_set(GPIO_ID_PIN(0), GPIO_PIN_INTR_DISABLE);
	//wdt_feed();
	
	gpio_status = GPIO_REG_READ(GPIO_STATUS_ADDRESS);
	//clear interrupt status
	GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, gpio_status);
	
	os_delay_us(1000);	// wait 1 mS to avoid reading on slope
	impulse_pin_state = GPIO_REG_READ(GPIO_IN_ADDRESS) & BIT0;
	if (impulse_pin_state) {	// rising edge
		impulse_rising_edge_time = system_get_time();
		
		if (impulse_rising_edge_time > impulse_falling_edge_time) {
			impulse_edge_to_edge_time = impulse_rising_edge_time - impulse_falling_edge_time;
		}
		else {
			// system time wrapped
#ifdef DEBUG
		os_printf("wrapped\n");
#endif
			impulse_edge_to_edge_time = UINT32_MAX - impulse_falling_edge_time + impulse_rising_edge_time;
		}
		
		// check if impulse period is 100 mS...
#ifdef DEBUG
		os_printf("imp: %uuS\n", impulse_rising_edge_time - impulse_falling_edge_time);
#endif	// DEBUG
		if ((impulse_edge_to_edge_time > 80 * 1000) && (impulse_edge_to_edge_time < 120 * 1000)) {
			// arm the debounce timer to enable GPIO interrupt again
			sys_cfg.impulse_meter_count++;
			os_timer_disarm(&impulse_meter_calculate_timer);
			os_timer_setfn(&impulse_meter_calculate_timer, (os_timer_func_t *)impulse_meter_calculate_timer_func, NULL);
			os_timer_arm(&impulse_meter_calculate_timer, 100, 0);
		}
	}
	else {						// falling edge
		impulse_falling_edge_time = system_get_time();
	}

	// enable gpio interrupt again
	gpio_pin_intr_state_set(GPIO_ID_PIN(0), GPIO_PIN_INTR_ANYEDGE);	// Interrupt on falling GPIO0 edge
	ETS_GPIO_INTR_ENABLE();
}

ICACHE_FLASH_ATTR
void impulse_meter_init(void) {
	os_strncpy(impulse_meter_serial, sys_cfg.impulse_meter_serial, IMPULSE_METER_SERIAL_LEN);
	
	impulse_meter_energy = atoi(sys_cfg.impulse_meter_energy);
	
	impulses_per_kwh = atoi(sys_cfg.impulses_per_kwh);
	if (impulses_per_kwh == 0) {
		impulses_per_kwh = 100;		// if not set set to some default != 0
	}
	
	impulse_time = uptime();
	last_impulse_time = 0;
	
	last_impulse_meter_count = sys_cfg.impulse_meter_count;

#ifdef POWER_WD
	// start power watch dog
	vdd_init = system_get_vdd33();
	os_printf("\n\rvdd init: %d\n\r", vdd_init);
	
	os_timer_disarm(&power_wd_timer);
	os_timer_setfn(&power_wd_timer, (os_timer_func_t *)power_wd_timer_func, NULL);
	os_timer_arm(&power_wd_timer, 50, 1);
#endif
#ifdef DEBUG
	os_printf("t: %u\n", impulse_time);
#endif // DEBUG
}

ICACHE_FLASH_ATTR
unsigned int impulse_meter_decimal_number_length(int n) {
	int digits;
	
	digits = n < 0;	//count "minus"
	do {
		digits++;
	} while (n /= 10);
	
	return digits;
}
#endif // IMPULSE

ICACHE_FLASH_ATTR void user_init(void) {
	uart_init(BIT_RATE_1200, BIT_RATE_1200);
	os_printf("\n\r");
	os_printf("SDK version: %s\n\r", system_get_sdk_version());
	os_printf("Software version: %s\n\r", VERSION);
#ifdef DEBUG
	os_printf("\t(DEBUG)\n\r");
#endif
#ifdef IMPULSE
	os_printf("\t(IMPULSE)\n\r");
#endif
#ifdef DEBUG_NO_METER
	os_printf("\t(DEBUG_NO_METER)\n\r");
#endif
#ifdef DEBUG_MQTT_PING
	os_printf("\t(DEBUG_MQTT_PING)\n\r");
#endif
#ifdef DEBUG_SHORT_WEB_CONFIG_TIME
	os_printf("\t(DEBUG_SHORT_WEB_CONFIG_TIME)\n\r");
#endif
#ifdef THERMO_NO
	os_printf("\t(THERMO_NO)\n\r");
#else
	os_printf("\t(THERMO_NC)\n\r");
#endif
#ifdef POWER_WD
	os_printf("\t(POWER_WD)\n\r");
#endif

#ifndef DEBUG
	// disable serial debug
	system_set_os_print(0);
#endif

	cfg_load();

	// start kmp_request
#ifdef EN61107
	en61107_request_init();
#elif defined IMPULSE
	impulse_meter_init();
#else
	kmp_request_init();
#endif
	
	// initialize the GPIO subsystem
	gpio_init();
	// enable gpio interrupt for impulse meters
#ifdef IMPULSE
	gpio_int_init();
#endif // IMPULSE
	
	ac_out_init();

	led_init();
	cron_init();
	
	// load thermo motor state from flash(AC OUT 1)
	if (sys_cfg.ac_thermo_state) {
		ac_thermo_open();
	}
	else {
		ac_thermo_close();
	}
	
	// make sure the device is in AP and STA combined mode
    wifi_set_opmode_current(STATIONAP_MODE);
	
	// do everything else in system_init_done
	system_init_done_cb(&system_init_done);
}

ICACHE_FLASH_ATTR void system_init_done(void) {
	rtc_info = system_get_rst_info();
#ifdef DEBUG
	os_printf("rst: %d\n", (rtc_info != NULL) ? rtc_info->reason : -1);
#endif	// DEBUG
	
	init_unix_time();

	// wait 10 seconds before starting wifi and let the meter boot
	// and send serial number request
#ifdef EN61107
	os_timer_disarm(&en61107_request_send_timer);
	os_timer_setfn(&en61107_request_send_timer, (os_timer_func_t *)en61107_request_send_timer_func, NULL);
	os_timer_arm(&en61107_request_send_timer, 10000, 0);
#elif defined IMPULSE
	// do nothing here
#else
	os_timer_disarm(&kmp_request_send_timer);
	os_timer_setfn(&kmp_request_send_timer, (os_timer_func_t *)kmp_request_send_timer_func, NULL);
	os_timer_arm(&kmp_request_send_timer, 10000, 0);
#endif

	if ((rtc_info != NULL) && (rtc_info->reason != REASON_DEFAULT_RST) && (rtc_info->reason != REASON_EXT_SYS_RST)) {
		// fast boot if reset, go in sample/station mode
#ifdef DEBUG
		os_printf("fast boot\n");
#endif
		os_timer_disarm(&sample_mode_timer);
		os_timer_setfn(&sample_mode_timer, (os_timer_func_t *)sample_mode_timer_func, NULL);
#ifdef EN61107
		os_timer_arm(&sample_mode_timer, 12000, 0);
#elif defined IMPULSE
		os_timer_arm(&sample_mode_timer, 100, 0);
#else
		os_timer_arm(&sample_mode_timer, 12000, 0);
#endif
	}
	else {
#ifdef DEBUG
		os_printf("normal boot\n");
#endif
#ifdef IMPULSE
		// start config mode at boot - dont wait for impulse based meters        
		os_timer_disarm(&config_mode_timer);
		os_timer_setfn(&config_mode_timer, (os_timer_func_t *)config_mode_timer_func, NULL);
		os_timer_arm(&config_mode_timer, 100, 0);
#else
		// start waiting for serial number after 16 seconds
		// and start ap mode
		os_timer_disarm(&config_mode_timer);
		os_timer_setfn(&config_mode_timer, (os_timer_func_t *)config_mode_timer_func, NULL);
		os_timer_arm(&config_mode_timer, 16000, 0);
#endif

		// wait for 120 seconds from boot and go to station mode
		os_timer_disarm(&sample_mode_timer);
		os_timer_setfn(&sample_mode_timer, (os_timer_func_t *)sample_mode_timer_func, NULL);
#ifndef DEBUG_SHORT_WEB_CONFIG_TIME
		os_timer_arm(&sample_mode_timer, 120000, 0);
#else
		os_timer_arm(&sample_mode_timer, 18000, 0);
#endif
	}
		
	INFO("\r\nSystem started ...\r\n");
}

