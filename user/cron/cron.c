#ifndef NO_CRON

#include <esp8266.h>

#include <time.h>
#include "unix_time.h"
#include "ac/ac_out.h"
#include "cron/cron.h"
#include "config.h"
#include "mqtt.h"
#include "wifi.h"
#include "user_main.h"

static os_timer_t minute_timer;
char sec_drift;

ICACHE_FLASH_ATTR
void static minute_timer_func(void *arg) {
	struct tm *dt;
	time_t unix_time;
	unsigned char i;
	unsigned char run_command;
	
	bool dst = false;
	int previous_sunday;
	int s;

	uint16_t calculated_crc;
	uint16_t saved_crc;

	if (get_unix_time() == 0) {
#ifdef DEBUG
		printf("skipping cron - no ntp time\n");
#endif
		return;
	}
	
	unix_time = get_unix_time() + (1 * 60 * 60);	// UTC + 1
	dt = gmtime(&unix_time);
	
	// compensate for daylight savings in Europe/Copenhagen
	// thanx to Richard A Burton
	// https://github.com/raburton/esp8266/blob/master/ntp/timezone.c
	if (dt->tm_mon < 2 || dt->tm_mon > 9) {
		// these months are completely out of DST
	}
	else if (dt->tm_mon > 2 && dt->tm_mon < 9) {
		// these months are completely in DST
		dst = true;
	}
	else {
		// else we must be in one of the change months
		// work out when the last sunday was (could be today)
		previous_sunday = dt->tm_mday - dt->tm_wday;
		if (dt->tm_mon == 2) { // march
			// was last sunday (which could be today) the last sunday in march
			if (previous_sunday >= 25) {
				// are we actually on the last sunday today
				if (dt->tm_wday == 0) {
					// if so are we at/past 2am gmt
					s = (dt->tm_hour * 3600) + (dt->tm_min * 60) + dt->tm_sec;
					if (s >= 7200) {
						dst = true;
					}
				} else {
					dst = true;
				}
			}
		}
		else if (dt->tm_mon == 9) {
			// was last sunday (which could be today) the last sunday in october
			if (previous_sunday >= 25) {
				// we have reached/passed it, so is it today?
				if (dt->tm_wday == 0) {
					// change day, so are we before 1am gmt (2am localtime)
					s = (dt->tm_hour * 3600) + (dt->tm_min * 60) + dt->tm_sec;
					if (s < 3600) {
						dst = true;
					}
				}
			}
			else {
				// not reached the last sunday yet
				dst = true;
			}
		}
	}
	
	if (dst) {
		// add the dst hour
		unix_time += (1 * 60 * 60);
		// call gmtime on changed value
		dt = gmtime(&unix_time);
		// don't rely on isdst returned by mktime, it doesn't know about timezones and tends to reset this to 0
		dt->tm_isdst = 1;
#ifdef DEBUG
	os_printf("UTC+1 DST+1: ");
#endif
	}
	else {
		dt->tm_isdst = 0;
#ifdef DEBUG
	os_printf("UTC+1: ");
#endif
	}
#ifdef DEBUG
	os_printf("%02d:%02d:%02d %d.%d.%d\r\n", dt->tm_hour, dt->tm_min, dt->tm_sec, dt->tm_mday, dt->tm_mon + 1, dt->tm_year + 1900);
#endif
	
	// sync to ntp time
	if (dt->tm_sec == 0) {
		if (sec_drift) {
			// we have run the timer with sec_drift interval, run it at normal interval again
			os_timer_disarm(&minute_timer);
			os_timer_setfn(&minute_timer, (os_timer_func_t *)minute_timer_func, NULL);
			os_timer_arm(&minute_timer, 60000, 1);
			sec_drift = 0;
#ifdef DEBUG
			//os_printf("normal\n\r");
#endif
		}
	}
	else {
		sec_drift = (60 - dt->tm_sec);
		os_timer_disarm(&minute_timer);
		os_timer_setfn(&minute_timer, (os_timer_func_t *)minute_timer_func, NULL);
		os_timer_arm(&minute_timer, (sec_drift * 1000), 0);
#ifdef DEBUG
		//os_printf("adjusting by %d s\n\r", sec_drift);
#endif
	}
	
	// check if any jobs should have run the last minute
	for (i = 0; i < sys_cfg.cron_jobs.n; i++) {
		run_command = 0;
#ifdef DEBUG
		/*
		os_printf("j: %u\t%s:%s\t%s\t %s\t%s\tc: %s\n\r", 
			i,
			sys_cfg.cron_jobs.cron_job_list[i].hour,
			sys_cfg.cron_jobs.cron_job_list[i].minute,
			sys_cfg.cron_jobs.cron_job_list[i].day_of_month,
			sys_cfg.cron_jobs.cron_job_list[i].month,
			sys_cfg.cron_jobs.cron_job_list[i].day_of_week,
			sys_cfg.cron_jobs.cron_job_list[i].command);
		*/
#endif
		// check cron jobs hour value
		if (strncmp(sys_cfg.cron_jobs.cron_job_list[i].hour, "*", CRON_FIELD_L) == 0) {
			run_command++;
		}
		else if (strncmp(sys_cfg.cron_jobs.cron_job_list[i].hour, "", CRON_FIELD_L) == 0) {
			run_command++;
		}
		else if (dt->tm_hour == atoi(sys_cfg.cron_jobs.cron_job_list[i].hour)) {
			run_command++;
		}
		
		// check cron jobs minute value
		if (strncmp(sys_cfg.cron_jobs.cron_job_list[i].minute, "*", CRON_FIELD_L) == 0) {
			run_command++;
		}
		else if (strncmp(sys_cfg.cron_jobs.cron_job_list[i].minute, "", CRON_FIELD_L) == 0) {
			run_command++;
		}
		else if (dt->tm_min == atoi(sys_cfg.cron_jobs.cron_job_list[i].minute)) {
			run_command++;
		}
		
		// check cron jobs day_of_month value
		if (strncmp(sys_cfg.cron_jobs.cron_job_list[i].day_of_month, "*", CRON_FIELD_L) == 0) {
			run_command++;
		}
		else if (strncmp(sys_cfg.cron_jobs.cron_job_list[i].day_of_month, "", CRON_FIELD_L) == 0) {
			run_command++;
		}
		else if (dt->tm_mday == atoi(sys_cfg.cron_jobs.cron_job_list[i].day_of_month)) {
			run_command++;
		}
		
		// check cron jobs month value
		if (strncmp(sys_cfg.cron_jobs.cron_job_list[i].month, "*", CRON_FIELD_L) == 0) {
			run_command++;
		}
		else if (strncmp(sys_cfg.cron_jobs.cron_job_list[i].month, "", CRON_FIELD_L) == 0) {
			run_command++;
		}
		else if ((dt->tm_mon + 1) == atoi(sys_cfg.cron_jobs.cron_job_list[i].month)) {
			run_command++;
		}
		
		// check cron jobs day_of_week value
		if (strncmp(sys_cfg.cron_jobs.cron_job_list[i].day_of_week, "*", CRON_FIELD_L) == 0) {
			run_command++;
		}
		else if (strncmp(sys_cfg.cron_jobs.cron_job_list[i].day_of_week, "", CRON_FIELD_L) == 0) {
			run_command++;
		}
		else if (dt->tm_wday == atoi(sys_cfg.cron_jobs.cron_job_list[i].day_of_week) % 7) {		// (0 or 7 is Sun therefore % 7
			run_command++;
		}

		// we need to run the command
		if (run_command == 5) {
#ifdef DEBUG
			os_printf("run: %s\n\r", sys_cfg.cron_jobs.cron_job_list[i].command);
#endif
			if (strncmp(sys_cfg.cron_jobs.cron_job_list[i].command, "open", COMMAND_L) == 0) {
				//ac_motor_valve_open();
				ac_thermo_open();
			}
			else if (strncmp(sys_cfg.cron_jobs.cron_job_list[i].command, "close", COMMAND_L) == 0) {
				//ac_motor_valve_close();
				ac_thermo_close();
			}
			else if (strncmp(sys_cfg.cron_jobs.cron_job_list[i].command, "set_ssid_pwd", COMMAND_L) == 0) {
				if (!cfg_save_ssid_pwd(sys_cfg.cron_jobs.cron_job_list[i].command_params, &calculated_crc, &saved_crc)) {
					mqtt_flash_error(calculated_crc, saved_crc);
				}
			}
			else if (strncmp(sys_cfg.cron_jobs.cron_job_list[i].command, "reconnect", COMMAND_L) == 0) {
				// reconnect with new password
				if (mqtt_client.pCon != NULL) {
					// if mqtt_client is initialized)
					MQTT_Disconnect(&mqtt_client);
				}
			}
			else if (strncmp(sys_cfg.cron_jobs.cron_job_list[i].command, "start_ap", COMMAND_L) == 0) {
				// start AP
				if (wifi_get_opmode() != STATIONAP_MODE) {
					wifi_set_opmode_current(STATIONAP_MODE);
					wifi_softap_config(mesh_ssid, AP_MESH_PASS, AP_MESH_TYPE);
					wifi_softap_ip_config();
	
					// ...and save setting to flash if changed
					if (sys_cfg.ap_enabled == false) {
						sys_cfg.ap_enabled = true;
						cfg_save(NULL, NULL);
					}
				}
			}
			else if (strncmp(sys_cfg.cron_jobs.cron_job_list[i].command, "stop_ap", COMMAND_L) == 0) {
				// stop AP
				if (wifi_get_opmode() != STATION_MODE) {
					wifi_set_opmode_current(STATION_MODE);
		
					// ...and save setting to flash if changed
					if (sys_cfg.ap_enabled == true) {
						sys_cfg.ap_enabled = false;
						cfg_save(NULL, NULL);
					}
				}
			}
			else if (strncmp(sys_cfg.cron_jobs.cron_job_list[i].command, "clear_cron", COMMAND_L) == 0) {
				clear_cron_jobs();
			}
			else if (strncmp(sys_cfg.cron_jobs.cron_job_list[i].command, "off", COMMAND_L) == 0) {
				ac_off();
			}
			else if (strncmp(sys_cfg.cron_jobs.cron_job_list[i].command, "test", COMMAND_L) == 0) {
				ac_test();
			}
		}
	}
}

ICACHE_FLASH_ATTR
void cron_init() {
	//memset(&sys_cfg.cron_jobs, 0, sizeof(cron_job_t));

	// check if there are any cron jobs to run every minute
	os_timer_disarm(&minute_timer);
	os_timer_setfn(&minute_timer, (os_timer_func_t *)minute_timer_func, NULL);
	os_timer_arm(&minute_timer, 60000, 1);
	
#ifdef DEBUG
	os_printf("\n\rcron jobs: %d\n\r", sys_cfg.cron_jobs.n);
#endif
}

ICACHE_FLASH_ATTR
unsigned int add_cron_job_from_query(char *query) {
	char cron_params[MQTT_MESSAGE_L];
	char *str;
	char *key, *value;
	char key_value[KEY_VALUE_L];
	char command_params[COMMAND_PARAMS_L];
	char *context_query_string, *context_key_value;
	
	strncpy(cron_params, query, MQTT_MESSAGE_L);	// work with a copy
	
	command_params[0] = 0;  // set zero length
	if (sys_cfg.cron_jobs.n <= CRON_JOBS_MAX - 1) {		// if there are room for more cron jobs
		memset(&sys_cfg.cron_jobs.cron_job_list[sys_cfg.cron_jobs.n], 0, sizeof(cron_job_t));
		
		str = strstr(cron_params, "command=") + 8;						// get string after command=
		if (str != NULL) {
			str = strtok_r(str, "=", &context_query_string);		// get function name
			if (str != NULL) {
				str = strtok_r(NULL, "", &context_query_string);	// get paramters to function
				if (str != NULL) {
					strncpy(command_params, str, COMMAND_PARAMS_L);
					memcpy(cron_params + (str - cron_params), "\0", 1);			// truncate cron_params after function
				}
			}
		}
		
		// parse mqtt message for query string
		str = strtok_r(cron_params, "&", &context_query_string);
		while (str != NULL) {
			strncpy(key_value, str, KEY_VALUE_L);
			key = strtok_r(key_value, "=", &context_key_value);
			value = strtok_r(NULL, "=", &context_key_value);
			if (strncmp(key, "minute", KEY_VALUE_L) == 0) {
				strncpy(sys_cfg.cron_jobs.cron_job_list[sys_cfg.cron_jobs.n].minute, value, CRON_FIELD_L);
			}
			else if (strncmp(key, "hour", KEY_VALUE_L) == 0) {
				strncpy(sys_cfg.cron_jobs.cron_job_list[sys_cfg.cron_jobs.n].hour, value, CRON_FIELD_L);
			}
			else if (strncmp(key, "day_of_month", KEY_VALUE_L) == 0) {
				strncpy(sys_cfg.cron_jobs.cron_job_list[sys_cfg.cron_jobs.n].day_of_month, value, CRON_FIELD_L);
			}
			else if (strncmp(key, "month", KEY_VALUE_L) == 0) {
				strncpy(sys_cfg.cron_jobs.cron_job_list[sys_cfg.cron_jobs.n].month, value, CRON_FIELD_L);
			}
			else if (strncmp(key, "day_of_week", KEY_VALUE_L) == 0) {
				strncpy(sys_cfg.cron_jobs.cron_job_list[sys_cfg.cron_jobs.n].day_of_week, value, CRON_FIELD_L);
			}
			else if (strncmp(key, "command", COMMAND_L) == 0) {
				strncpy(sys_cfg.cron_jobs.cron_job_list[sys_cfg.cron_jobs.n].command, value, COMMAND_L);
#ifdef DEBUG
				os_printf("command: %s\n\r", sys_cfg.cron_jobs.cron_job_list[sys_cfg.cron_jobs.n].command);
#endif
				if (strlen(command_params)) {
					strncpy(sys_cfg.cron_jobs.cron_job_list[sys_cfg.cron_jobs.n].command_params, command_params, COMMAND_PARAMS_L);
#ifdef DEBUG
					os_printf("command_params: %s\n\r", sys_cfg.cron_jobs.cron_job_list[sys_cfg.cron_jobs.n].command_params);
#endif
				}
				sys_cfg.cron_jobs.n++;
#ifdef DEBUG
				os_printf("job #%d added\n\r", sys_cfg.cron_jobs.n);
#endif
			}
			
			str = strtok_r(NULL, "&", &context_query_string);
		}
		// save it to flash
		cfg_save_defered();
	}
	
	return sys_cfg.cron_jobs.n;
}

ICACHE_FLASH_ATTR
void clear_cron_jobs() {
#ifdef DEBUG
	//debug_cron_jobs();
#endif
	memset(&sys_cfg.cron_jobs, 0, sizeof(cron_job_t));
	
	// save it to flash
	cfg_save_defered();
#ifdef DEBUG
	os_printf("\n\rcleared all jobs\n\r");
#endif
}

ICACHE_FLASH_ATTR
void cron_destroy() {
	os_timer_disarm(&minute_timer);
}

#ifdef DEBUG
ICACHE_FLASH_ATTR
void debug_cron_jobs() {
	unsigned char i;
	
	for (i = 0; i < sys_cfg.cron_jobs.n; i++) { 
		os_printf("j: %u\t%s:%s\t%s\t %s\t%s\tc: %s\tp: %s\n\r", 
			i,
			sys_cfg.cron_jobs.cron_job_list[i].hour,
			sys_cfg.cron_jobs.cron_job_list[i].minute,
			sys_cfg.cron_jobs.cron_job_list[i].day_of_month,
			sys_cfg.cron_jobs.cron_job_list[i].month,
			sys_cfg.cron_jobs.cron_job_list[i].day_of_week,
			sys_cfg.cron_jobs.cron_job_list[i].command,
			sys_cfg.cron_jobs.cron_job_list[i].command_params);
	}
}
#endif

#endif	// NO_CRON