#ifndef APP_AUDIO_H
#define APP_AUDIO_H

#include <stdint.h>

typedef enum
{
    APP_AUDIO_IDLE,
    APP_AUDIO_PLAYING,
    APP_AUDIO_ERROR
} app_audio_state_t;

void app_audio_init(void);
void app_audio_poll(uint8_t alarm_active);
app_audio_state_t app_audio_get_state(void);

#endif
