#define TOPIC_L         64
#define MESSAGE_L       100
#define FUNCTIONNAME_L  32
#define KEY_VALUE_L     64
#define COMMAND_L       32

#define CRON_JOBS_MAX   32
#define CRON_FIELD_L    8

// sample mqtt topic and message
// char topic[] = "/config/v1/9999999/set_cron";
// char message[] = "minute=99&hour=06&day_of_month=*&month=*&day_of_week=*&command=open";

#ifndef CRON_H
#define CRON_H

typedef struct {
    char minute[CRON_FIELD_L];
    char hour[CRON_FIELD_L];
    char day_of_month[CRON_FIELD_L];
    char month[CRON_FIELD_L];
    char day_of_week[CRON_FIELD_L];
    char command[COMMAND_L];
} cron_job_t;

typedef cron_job_t cron_job_list_t[CRON_JOBS_MAX];

typedef struct {
    unsigned char n;
    cron_job_list_t cron_job_list;
} cron_jobs_t;

#endif /* CRON_H */

ICACHE_FLASH_ATTR void cron_init();
ICACHE_FLASH_ATTR unsigned int add_cron_job_from_query(char *query);
ICACHE_FLASH_ATTR void clear_cron_jobs();
//ICACHE_FLASH_ATTR void debug_cron_jobs();

