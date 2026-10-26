#ifndef APP_REPORT_H
#define APP_REPORT_H
#include <stdint.h>

typedef struct
{
    uint32_t interval_seconds, bad_commands, dropped_events;
} app_report_data_t;

void app_report_init(void);
void app_report_poll(void);
const app_report_data_t *app_report_get_data(void);
#endif
