#include "app_time.h"
#include "bsp_rtc.h"
#include "bsp_esp.h"
#include "debug_config.h"
#include "stm32f1xx_hal.h"

static uint32_t last_sequence, rtc_tick, log_tick;

void app_time_init(void)
{
    BSP_RTC_Init();
    BSP_ESP_Init();
    last_sequence = 0U;
    rtc_tick = log_tick = HAL_GetTick();
    DEBUG_PRINT(DEBUG_LOG_TIME, "[RTC] boot valid=%u restored=%u; local=UTC+8\r\n",
                (unsigned int)BSP_RTC_GetData()->valid, (unsigned int)BSP_RTC_GetData()->restored);
}

void app_time_poll(void)
{
    uint32_t now = HAL_GetTick();
    const bsp_esp_data_t *esp;
    const bsp_rtc_data_t *rtc;

    BSP_ESP_Poll();
    esp = BSP_ESP_GetData();
    if (esp->time_sequence != last_sequence)
    {
        last_sequence = esp->time_sequence;
        if (BSP_RTC_Set(&esp->time))
            DEBUG_PRINT(DEBUG_LOG_TIME, "[RTC] SNTP applied, local=UTC+8\r\n");
        else
            DEBUG_PRINT(DEBUG_LOG_ERRORS || DEBUG_LOG_TIME, "[RTC] SNTP valid but RTC write failed\r\n");
    }
    if ((uint32_t)(now - rtc_tick) >= 200U)
    {
        rtc_tick = now;
        BSP_RTC_Poll();
    }
    if ((uint32_t)(now - log_tick) < DEBUG_TIME_PERIOD_MS)
        return;
    log_tick = now;
    rtc = BSP_RTC_GetData();
    if (rtc->valid)
        DEBUG_PRINT(DEBUG_LOG_TIME, "[RTC] %04u-%02u-%02u %02u:%02u:%02u UTC+8 ts=%lu wifi=%u\r\n",
                    (unsigned int)rtc->local.year, (unsigned int)rtc->local.month,
                    (unsigned int)rtc->local.day, (unsigned int)rtc->local.hour,
                    (unsigned int)rtc->local.minute, (unsigned int)rtc->local.second,
                    (unsigned long)rtc->unix_utc, (unsigned int)esp->wifi_connected);
    else
        DEBUG_PRINT(DEBUG_LOG_TIME, "[RTC] UNCALIBRATED ts=0 esp=%u\r\n", (unsigned int)esp->state);
}
