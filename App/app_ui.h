#ifndef APP_UI_H
#define APP_UI_H

#include <stdint.h>

void app_ui_init(void);
void app_ui_next_page(void);
void app_ui_set_alarm(uint8_t active);
void app_ui_poll(void);

#endif
