#include "app_audio.h"
#include "audio/alarm_pcm.h"
#include "bsp_audio.h"
#include "bsp_log.h"

static uint8_t last_alarm;
static uint32_t last_errors;

_Static_assert(APP_ALARM_PCM_RATE_HZ == 16000U,
               "I2S2 is configured for 16 kHz");
_Static_assert(APP_ALARM_PCM_CHANNELS == 1U, "PCM must be stored as mono");
_Static_assert(APP_ALARM_PCM_FRAMES > 0U && APP_ALARM_PCM_FRAMES <= UINT32_MAX,
               "Invalid PCM streaming length");

void app_audio_init(void) {
  BSP_AUDIO_Init();
  last_alarm = 0U;
  last_errors = 0U;
}

void app_audio_poll(uint8_t alarm_active) {
  const bsp_audio_data_t *audio;

  alarm_active = alarm_active ? 1U : 0U;
  BSP_AUDIO_Poll();
  if (alarm_active != last_alarm) {
    last_alarm = alarm_active;
    if (alarm_active) {
      if (BSP_AUDIO_PlayLoopWithGap(app_alarm_pcm, APP_ALARM_PCM_FRAMES,
                                    APP_AUDIO_REPEAT_GAP_MS))
        DEBUG_PRINT(DEBUG_LOG_AUDIO,
                    "[AUDIO] alarm started: 16kHz mono, repeat gap=%lu ms\r\n",
                    (unsigned long)APP_AUDIO_REPEAT_GAP_MS);
    } else {
      BSP_AUDIO_Stop();
      DEBUG_PRINT(DEBUG_LOG_AUDIO, "[AUDIO] stopped\r\n");
    }
  }
  /* An error stays latched during this alarm; clearing it allows the next alarm
   * to retry. */
  audio = BSP_AUDIO_GetData();
  if (audio->errors != last_errors) {
    last_errors = audio->errors;
    DEBUG_PRINT(DEBUG_LOG_ERRORS || DEBUG_LOG_AUDIO,
                "[AUDIO] error=0x%08lX count=%lu\r\n",
                (unsigned long)audio->last_error, (unsigned long)audio->errors);
  }
}

app_audio_state_t app_audio_get_state(void) {
  switch (BSP_AUDIO_GetData()->state) {
  case BSP_AUDIO_PLAYING:
    return APP_AUDIO_PLAYING;
  case BSP_AUDIO_ERROR:
    return APP_AUDIO_ERROR;
  default:
    return APP_AUDIO_IDLE;
  }
}
