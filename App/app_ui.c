#include "app_ui.h"
#include "app_fall.h"
#include "app_voice.h"
#include "debug_config.h"
#include "bsp_oled.h"
#include "bsp_gps.h"
#include "bsp_mpu6050.h"
#include "stm32f1xx_hal.h"
#include <stdio.h>

static uint8_t page;
static uint8_t alarm_active;
static uint8_t dirty;
static uint32_t last_refresh;
static bsp_oled_state_t last_oled_state;

void app_ui_init(void)
{
    page = 5U;
    alarm_active = 0U;
    dirty = 1U;
    last_refresh = HAL_GetTick();
    last_oled_state = BSP_OLED_WAIT_POWER;
    BSP_OLED_Init();
}

void app_ui_next_page(void)
{
    page = (uint8_t)((page + 1U) % 6U);
    dirty = 1U;
}

void app_ui_set_alarm(uint8_t active)
{
    if (alarm_active != active)
    {
        alarm_active = active;
        dirty = 1U;
    }
}

static void draw_mpu_page(void)
{
    static const char *const accel_labels[] = {"AX", "AY", "AZ"};
    static const char *const gyro_labels[] = {"GX", "GY", "GZ"};
    static const char *const angle_labels[] = {"Roll ", "Pitch"};
    const bsp_mpu6050_sample_t *sample = BSP_MPU6050_GetSample();
    bsp_mpu6050_state_t state = BSP_MPU6050_GetState();
    uint8_t valid = (state == BSP_MPU6050_READY) && (sample->samples != 0U);
    char line[32];
    const char *title = "MPU6050 INIT";

    if (state == BSP_MPU6050_CALIBRATING)
        title = "MPU6050 CAL...";
    else if (state == BSP_MPU6050_ERROR)
        title = "MPU6050 ERROR";
    else if (valid)
        title = "MPU6050 LIVE";
    BSP_OLED_ShowString(0U, 0U, title, BSP_OLED_6X8);
    for (uint8_t i = 0U; i < 3U; ++i)
    {
        if (valid)
        {
            long value = (long)sample->accel_mg[i];
            long magnitude = value < 0 ? -value : value;
            snprintf(line, sizeof(line), "%s:%c%ld.%03ld", accel_labels[i], value < 0 ? '-' : '+',
                     magnitude / 1000L, magnitude % 1000L);
        }
        else
            snprintf(line, sizeof(line), "%s:--", accel_labels[i]);
        BSP_OLED_ShowString(0U, (uint8_t)(8U + 8U*i), line, BSP_OLED_6X8);
        if (valid)
        {
            long value = (long)sample->gyro_mdps[i];
            long magnitude = value < 0 ? -value : value;
            snprintf(line, sizeof(line), "%s:%c%ld.%02ld", gyro_labels[i], value < 0 ? '-' : '+',
                     magnitude / 1000L, (magnitude % 1000L) / 10L);
        }
        else
            snprintf(line, sizeof(line), "%s:--", gyro_labels[i]);
        BSP_OLED_ShowString(66U, (uint8_t)(8U + 8U*i), line, BSP_OLED_6X8);
    }
    for (uint8_t i = 0U; i < 2U; ++i)
    {
        if (valid)
        {
            long value = (long)sample->angle_cdeg[i];
            long magnitude = value < 0 ? -value : value;
            snprintf(line, sizeof(line), "%s: %c%ld.%02ld deg", angle_labels[i], value < 0 ? '-' : '+',
                     magnitude / 100L, magnitude % 100L);
        }
        else
            snprintf(line, sizeof(line), "%s: -- deg", angle_labels[i]);
        BSP_OLED_ShowString(0U, (uint8_t)(32U + 8U*i), line, BSP_OLED_6X8);
    }
    BSP_OLED_ShowString(0U, 48U, "A:g G:deg/s", BSP_OLED_6X8);
    if (valid)
    {
        const app_fall_data_t *fall = app_fall_get_data();
        if (fall->state == APP_FALL_HOLDING)
            snprintf(line, sizeof(line), "HOLD:%lu/%lums", (unsigned long)fall->held_ms,
                     (unsigned long)APP_FALL_HOLD_MS);
        else if (fall->state == APP_FALL_WAIT_UPRIGHT)
            snprintf(line, sizeof(line), "RAISE TO REARM");
        else
            snprintf(line, sizeof(line), "FALL:READY DT:%lu", (unsigned long)sample->interval_ms);
    }
    else
        snprintf(line, sizeof(line), state == BSP_MPU6050_ERROR ? "CHECK PB6/PB7 AD0=0" : "KEEP STILL (~3s)");
    BSP_OLED_ShowString(0U, 56U, line, BSP_OLED_6X8);
}

static void draw_page(uint32_t now)
{
    char line[32];

    BSP_OLED_Clear();
    if (alarm_active != 0U)
    {
        BSP_OLED_ShowString(24U, 0U,
                           app_fall_get_data()->source == APP_FALL_SOURCE_AUTO ? "FALL DEMO" : "TEST ALARM",
                           BSP_OLED_8X16);
        BSP_OLED_DrawLine(0U, 19U, 127U, 19U);
        BSP_OLED_ShowString(16U, 28U, "K2 TO CANCEL", BSP_OLED_8X16);
        {
            app_voice_state_t voice = app_voice_get_state();
            const char *message = "VOICE INIT";
            if (voice == APP_VOICE_PLAYING)
                message = "VOICE BUSY HIGH";
            else if (voice == APP_VOICE_WAIT_BUSY)
                message = "VOICE WAIT BUSY";
            else if (voice == APP_VOICE_ERROR)
                message = "VOICE TIMER ERROR";
            BSP_OLED_ShowString(10U, 52U, message, BSP_OLED_6X8);
        }
        BSP_OLED_Reverse();
        return;
    }

    if (page == 5U)
    {
        draw_mpu_page();
        return;
    }

    if (page == 0U)
    {
        const bsp_gps_data_t *g = BSP_GPS_GetData();
        long magnitude;
        const char *status = g->fix ? "LIVE" : (g->has_position ? "LAST" : "WAIT");

        snprintf(line, sizeof(line), "GPS %s  SAT:%u", status, (unsigned int)g->satellites);
        BSP_OLED_ShowString(0U, 0U, line, BSP_OLED_6X8);
        if (g->utc_valid)
        {
            snprintf(line, sizeof(line), "UTC 20%02u-%02u-%02u", (unsigned int)g->utc_year,
                     (unsigned int)g->utc_month, (unsigned int)g->utc_day);
            BSP_OLED_ShowString(0U, 8U, line, BSP_OLED_6X8);
            snprintf(line, sizeof(line), "    %02u:%02u:%02u", (unsigned int)g->utc_hour,
                     (unsigned int)g->utc_minute, (unsigned int)g->utc_second);
            BSP_OLED_ShowString(0U, 16U, line, BSP_OLED_6X8);
        }
        else
        {
            BSP_OLED_ShowString(0U, 8U, "UTC ----/--/--", BSP_OLED_6X8);
            BSP_OLED_ShowString(0U, 16U, "    --:--:--", BSP_OLED_6X8);
        }
        if (g->has_position)
        {
            magnitude = (g->latitude_udeg < 0) ? -(long)g->latitude_udeg : (long)g->latitude_udeg;
            snprintf(line, sizeof(line), "LAT %c%ld.%06ld", g->latitude_udeg < 0 ? '-' : '+',
                     magnitude / 1000000L, magnitude % 1000000L);
            BSP_OLED_ShowString(0U, 24U, line, BSP_OLED_6X8);
            magnitude = (g->longitude_udeg < 0) ? -(long)g->longitude_udeg : (long)g->longitude_udeg;
            snprintf(line, sizeof(line), "LON %c%ld.%06ld", g->longitude_udeg < 0 ? '-' : '+',
                     magnitude / 1000000L, magnitude % 1000000L);
            BSP_OLED_ShowString(0U, 32U, line, BSP_OLED_6X8);
            magnitude = (g->altitude_cm < 0) ? -(long)g->altitude_cm : (long)g->altitude_cm;
            snprintf(line, sizeof(line), "ALT %c%ld.%02ldm", g->altitude_cm < 0 ? '-' : '+',
                     magnitude / 100L, magnitude % 100L);
            BSP_OLED_ShowString(0U, 40U, line, BSP_OLED_6X8);
        }
        else
        {
            BSP_OLED_ShowString(0U, 24U, "LAT --", BSP_OLED_6X8);
            BSP_OLED_ShowString(0U, 32U, "LON --", BSP_OLED_6X8);
            BSP_OLED_ShowString(0U, 40U, "ALT --", BSP_OLED_6X8);
        }
        if (g->hdop_valid)
            snprintf(line, sizeof(line), "HDOP %lu.%02lu", (unsigned long)(g->hdop_x100 / 100U),
                     (unsigned long)(g->hdop_x100 % 100U));
        else
            snprintf(line, sizeof(line), "HDOP --");
        BSP_OLED_ShowString(0U, 48U, line, BSP_OLED_6X8);
        if (g->has_position && !g->fix)
            snprintf(line, sizeof(line), "LAST AGE %lus", (unsigned long)(g->age_ms / 1000U));
        else
            snprintf(line, sizeof(line), "K1:PAGE  K2:ALARM");
        BSP_OLED_ShowString(0U, 56U, line, BSP_OLED_6X8);
    }
    else if (page == 1U)
    {
        const bsp_gps_data_t *g = BSP_GPS_GetData();
        BSP_OLED_ShowString(0U, 0U, "GPS SIGNAL", BSP_OLED_8X16);
        BSP_OLED_ShowString(0U, 20U, "LEVEL:", BSP_OLED_6X8);
        for (uint8_t i = 0U; i < 3U; ++i)
        {
            uint8_t height = (uint8_t)(4U + 4U * i);
            BSP_OLED_DrawRectangle((uint8_t)(48U + 14U * i), (uint8_t)(32U - height),
                                   6U, height, (g->signal_level > i) ?
                                   BSP_OLED_FILLED : BSP_OLED_UNFILLED);
        }
        if (g->gsv_valid)
        {
            snprintf(line, sizeof(line), "VIEW:%u  CN0 N:%u", (unsigned int)g->satellites_in_view,
                     (unsigned int)g->signal_count);
            BSP_OLED_ShowString(0U, 44U, line, BSP_OLED_6X8);
        }
        else
            BSP_OLED_ShowString(0U, 44U, "WAITING FOR GSV", BSP_OLED_6X8);
        if (g->signal_valid)
            snprintf(line, sizeof(line), "MAX C/N0:%u dBHz", (unsigned int)g->signal_max_dbhz);
        else
            snprintf(line, sizeof(line), "MAX C/N0:--");
        BSP_OLED_ShowString(0U, 34U, line, BSP_OLED_6X8);
        snprintf(line, sizeof(line), "FIX:%u  USED:%u", (unsigned int)g->fix,
                 (unsigned int)g->satellites);
        BSP_OLED_ShowString(0U, 54U, line, BSP_OLED_6X8);
    }
    else if (page == 2U)
    {
        BSP_OLED_ShowString(0U, 0U, "FALLGUARD C05", BSP_OLED_8X16);
        BSP_OLED_ShowString(0U, 20U, "SSD1306 / SOFT I2C", BSP_OLED_6X8);
        snprintf(line, sizeof(line), "UP %lus", (unsigned long)(now / 1000U));
        BSP_OLED_ShowString(0U, 34U, line, BSP_OLED_6X8);
        BSP_OLED_ShowString(0U, 52U, "K1:PAGE  K2:ALARM", BSP_OLED_6X8);
    }
    else if (page == 3U)
    {
        BSP_OLED_ShowString(0U, 0U, "CHINESE", BSP_OLED_8X16);
        BSP_OLED_ShowChinese(16U, 22U, "你好，世界。");
        BSP_OLED_ShowString(0U, 52U, "UTF-8 / 16x16", BSP_OLED_6X8);
    }
    else
    {
        uint8_t width = (uint8_t)((now / 100U) % 109U);

        BSP_OLED_DrawRectangle(0U, 0U, 128U, 64U, BSP_OLED_UNFILLED);
        BSP_OLED_ShowString(4U, 3U, "GRAPHICS", BSP_OLED_6X8);
        BSP_OLED_DrawLine(4U, 15U, 123U, 15U);
        BSP_OLED_DrawRectangle(8U, 22U, 28U, 22U, BSP_OLED_UNFILLED);
        BSP_OLED_DrawCircle(60U, 34U, 11U, BSP_OLED_UNFILLED);
        BSP_OLED_DrawRectangle(88U, 23U, 24U, 20U, BSP_OLED_FILLED);
        BSP_OLED_DrawRectangle(8U, 52U, 112U, 8U, BSP_OLED_UNFILLED);
        BSP_OLED_DrawRectangle(10U, 54U, width, 4U, BSP_OLED_FILLED);
    }

}

void app_ui_poll(void)
{
    bsp_oled_state_t state;
    uint32_t now = HAL_GetTick();

    BSP_OLED_Poll();
    state = BSP_OLED_GetState();
    if (state != last_oled_state)
    {
        last_oled_state = state;
        if (state == BSP_OLED_READY)
        {
            DEBUG_PRINT(DEBUG_LOG_STATUS, "[C05] OLED init ACK OK @0x3C\r\n");
        }
        else if (state == BSP_OLED_ERROR)
        {
            DEBUG_PRINT(DEBUG_LOG_ERRORS, "[C05] OLED bus/ACK failed; check PB4/PB5, pull-ups and 0x3C\r\n");
        }
    }

    /* One framebuffer is sufficient when drawing waits for the current transfer. */
    if ((state == BSP_OLED_READY) && (BSP_OLED_IsBusy() == 0U) &&
        ((dirty != 0U) || ((uint32_t)(now - last_refresh) >= 100U)))
    {
        draw_page(now);
        BSP_OLED_Update();
        dirty = 0U;
        last_refresh = now;
    }
}
