#ifndef APP_VOICE_H
#define APP_VOICE_H

#include <stdint.h>

#define APP_VOICE_ALARM_TRACK 1U
#define APP_VOICE_VOLUME      20U

typedef enum
{
    APP_VOICE_BOOT,
    APP_VOICE_SELECTING,
    APP_VOICE_STOPPING,
    APP_VOICE_CONFIGURING,
    APP_VOICE_IDLE,
    APP_VOICE_WAIT_BUSY,
    APP_VOICE_PLAYING,
    APP_VOICE_ERROR
} app_voice_state_t;

void app_voice_init(void);
void app_voice_poll(uint8_t alarm_active);
app_voice_state_t app_voice_get_state(void);

#endif
