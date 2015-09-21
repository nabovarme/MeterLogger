#include "ets_sys.h"
#include "osapi.h"
#include "user_interface.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "cron.h"

cron_jobs_t cron_jobs;

ICACHE_FLASH_ATTR
void cron_init() {
	memset(&cron_jobs, 0, sizeof(cron_jobs));
}

ICACHE_FLASH_ATTR
unsigned int add_cron_job_from_query(char *query) {
	char *str;
	char *key, *value;
	char key_value[KEY_VALUE_L];
	char *context_query_string, *context_key_value;
	
	// parse mqtt message for query string
	memset(&cron_jobs.cron_job_list[cron_jobs.n], 0, sizeof(cron_job_t));
	str = strtok_r(query, "&", &context_query_string);
	while (str != NULL) {
		strncpy(key_value, str, KEY_VALUE_L);
		key = strtok_r(key_value, "=", &context_key_value);
		value = strtok_r(NULL, "=", &context_key_value);
		if (strncmp(key, "minute", KEY_VALUE_L) == 0) {
			strncpy(cron_jobs.cron_job_list[cron_jobs.n].minute, value, CRON_FIELD_L);
		}
		else if (strncmp(key, "hour", KEY_VALUE_L) == 0) {
			strncpy(cron_jobs.cron_job_list[cron_jobs.n].hour, value, CRON_FIELD_L);
		}
		else if (strncmp(key, "day_of_month", KEY_VALUE_L) == 0) {
			strncpy(cron_jobs.cron_job_list[cron_jobs.n].day_of_month, value, CRON_FIELD_L);
		}
		else if (strncmp(key, "month", KEY_VALUE_L) == 0) {
			strncpy(cron_jobs.cron_job_list[cron_jobs.n].month, value, CRON_FIELD_L);
		}
		else if (strncmp(key, "day_of_week", KEY_VALUE_L) == 0) {
			strncpy(cron_jobs.cron_job_list[cron_jobs.n].day_of_week, value, CRON_FIELD_L);
		}
		else if (strncmp(key, "command", COMMAND_L) == 0) {
			strncpy(cron_jobs.cron_job_list[cron_jobs.n].command, value, COMMAND_L);
			cron_jobs.n++;
		}
		
		str = strtok_r(NULL, "&", &context_query_string);
	}
#ifdef DEBUG
	debug_cron_jobs();
#endif
	
	return cron_jobs.n;
}

ICACHE_FLASH_ATTR void clear_cron_jobs() {
	memset(&cron_jobs, 0, sizeof(cron_jobs));
#ifdef DEBUG
	os_printf("\n\rcleared all jobs\n\r");
#endif
}


ICACHE_FLASH_ATTR void debug_cron_jobs() {
	unsigned int i;
	
	i = cron_jobs.n;
	while (i) {
		os_printf("jobs: %u\n\rminute: %s\n\rhour: %s\n\rday_of_month: %s\n\rmonth: %s\n\rday_of_week: %s\n\rcommand: %s\n\r", 
			i,
			cron_jobs.cron_job_list[i - 1].minute,
			cron_jobs.cron_job_list[i - 1].hour,
			cron_jobs.cron_job_list[i - 1].day_of_month,
			cron_jobs.cron_job_list[i - 1].month,
			cron_jobs.cron_job_list[i - 1].day_of_week,
			cron_jobs.cron_job_list[i - 1].command);
		i--;
	}
}