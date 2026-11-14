#ifndef APP_UI_H
#define APP_UI_H

#include <stdint.h>

void app_ui_init(void);
typedef enum { APP_UI_BOOT_OK, APP_UI_BOOT_WARN, APP_UI_BOOT_WAIT } app_ui_boot_level_t;
/* Boot messages are copied; callers may release their text after the call. */
void app_ui_boot_log(const char *message, app_ui_boot_level_t level);
void app_ui_boot_hardware_done(void);
uint8_t app_ui_boot_active(void);
uint8_t app_ui_frame_pending(void);
void app_ui_next_page(void);
void app_ui_set_alarm(uint8_t active);
void app_ui_poll(void);

#endif
