#ifndef APP_FALL_H
#define APP_FALL_H

#include <stdint.h>

#define APP_FALL_ANGLE_DEG 60
#define APP_FALL_HOLD_MS   1000U

typedef enum
{
    APP_FALL_IDLE,
    APP_FALL_HOLDING,
    APP_FALL_ALARM,
    APP_FALL_WAIT_UPRIGHT
} app_fall_state_t;

typedef enum
{
    APP_FALL_SOURCE_NONE,
    APP_FALL_SOURCE_AUTO,
    APP_FALL_SOURCE_MANUAL
} app_fall_source_t;

typedef struct
{
    app_fall_state_t state;
    app_fall_source_t source;
    uint32_t held_ms;
} app_fall_data_t;

void app_fall_init(void);
void app_fall_poll(void);
void app_fall_toggle_alarm(void);
const app_fall_data_t *app_fall_get_data(void);

#endif
