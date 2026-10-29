#include "app_audio.h"
#include "audio/alarm_pcm.h"
#include "bsp_audio.h"
#include "debug_config.h"

static uint8_t last_alarm;
static uint32_t last_errors;

_Static_assert(APP_ALARM_PCM_RATE_HZ == 8000U, "I2S2 is configured for 8 kHz");
_Static_assert((APP_ALARM_PCM_WORDS % 2U) == 0U, "PCM must contain complete L/R frames");
_Static_assert(APP_ALARM_PCM_WORDS <= UINT16_MAX, "PCM exceeds one DMA transfer");

void app_audio_init(void)
{
    BSP_AUDIO_Init();
    last_alarm = 0U;
    last_errors = 0U;
}

void app_audio_poll(uint8_t alarm_active)
{
    const bsp_audio_data_t *audio;

    alarm_active = alarm_active ? 1U : 0U;
    BSP_AUDIO_Poll();
    if (alarm_active != last_alarm)
    {
        last_alarm = alarm_active;
        if (alarm_active)
        {
            if (BSP_AUDIO_PlayLoop(app_alarm_pcm, (uint16_t)APP_ALARM_PCM_WORDS))
                DEBUG_PRINT(DEBUG_LOG_AUDIO, "[AUDIO] alarm loop started: 8kHz PCM16 stereo\r\n");
        }
        else
        {
            BSP_AUDIO_Stop();
            DEBUG_PRINT(DEBUG_LOG_AUDIO, "[AUDIO] stopped\r\n");
        }
    }
    /* An error stays latched during this alarm; clearing it allows the next alarm to retry. */
    audio = BSP_AUDIO_GetData();
    if (audio->errors != last_errors)
    {
        last_errors = audio->errors;
        DEBUG_PRINT(DEBUG_LOG_ERRORS || DEBUG_LOG_AUDIO, "[AUDIO] error=0x%08lX count=%lu\r\n",
                    (unsigned long)audio->last_error, (unsigned long)audio->errors);
    }
}

app_audio_state_t app_audio_get_state(void)
{
    switch (BSP_AUDIO_GetData()->state)
    {
    case BSP_AUDIO_PLAYING: return APP_AUDIO_PLAYING;
    case BSP_AUDIO_ERROR: return APP_AUDIO_ERROR;
    default: return APP_AUDIO_IDLE;
    }
}
