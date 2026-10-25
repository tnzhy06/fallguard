#include "app_voice.h"
#include "bsp_jq8900.h"
#include "debug_config.h"

static app_voice_state_t state;
static uint8_t last_alarm;

void app_voice_init(void)
{
    BSP_JQ8900_Init();
    state = APP_VOICE_BOOT;
    last_alarm = 0U;
}

void app_voice_poll(uint8_t alarm_active)
{
    BSP_JQ8900_Poll();
    if (BSP_JQ8900_GetData()->error)
    {
        if (state != APP_VOICE_ERROR)
            DEBUG_PRINT(DEBUG_LOG_ERRORS, "[JQ] TIM3 waveform start failed\r\n");
        state = APP_VOICE_ERROR;
        return;
    }
    if (last_alarm != alarm_active)
    {
        last_alarm = alarm_active;
        if (!alarm_active && (state >= APP_VOICE_IDLE))
        {
            BSP_JQ8900_Stop();
            state = APP_VOICE_IDLE;
            DEBUG_PRINT(DEBUG_LOG_JQ, "[JQ] stop requested\r\n");
        }
    }
    switch (state)
    {
    case APP_VOICE_BOOT:
        if (BSP_JQ8900_GetData()->ready && BSP_JQ8900_SelectFlash())
            state = APP_VOICE_SELECTING;
        break;
    case APP_VOICE_SELECTING:
        if (!BSP_JQ8900_IsSending())
        {
            BSP_JQ8900_Stop();
            state = APP_VOICE_STOPPING;
        }
        break;
    case APP_VOICE_STOPPING:
        if (!BSP_JQ8900_IsSending() && BSP_JQ8900_SetVolume(APP_VOICE_VOLUME))
            state = APP_VOICE_CONFIGURING;
        break;
    case APP_VOICE_CONFIGURING:
        if (!BSP_JQ8900_IsSending())
        {
            state = APP_VOICE_IDLE;
            DEBUG_PRINT(DEBUG_LOG_JQ, "[JQ] command init sent; volume=%u BUSY=%u\r\n",
                        (unsigned int)APP_VOICE_VOLUME, (unsigned int)BSP_JQ8900_IsPlaying());
        }
        break;
    case APP_VOICE_IDLE:
        if (alarm_active && !BSP_JQ8900_IsSending() && BSP_JQ8900_Play(APP_VOICE_ALARM_TRACK))
        {
            state = APP_VOICE_WAIT_BUSY;
            DEBUG_PRINT(DEBUG_LOG_JQ, "[JQ] track=%lu queued; waiting for BUSY high\r\n",
                        (unsigned long)APP_VOICE_ALARM_TRACK);
        }
        break;
    case APP_VOICE_WAIT_BUSY:
        if (BSP_JQ8900_IsPlaying())
        {
            state = APP_VOICE_PLAYING;
            DEBUG_PRINT(DEBUG_LOG_JQ, "[JQ] BUSY high\r\n");
        }
        break;
    case APP_VOICE_PLAYING:
        /* Re-select only after a complete high-to-low playback indication. */
        if (!BSP_JQ8900_IsPlaying() && !BSP_JQ8900_IsSending())
        {
            state = APP_VOICE_IDLE;
            DEBUG_PRINT(DEBUG_LOG_JQ, "[JQ] BUSY low; repeat if alarm remains\r\n");
        }
        break;
    default:
        break;
    }
}

app_voice_state_t app_voice_get_state(void)
{
    return state;
}
