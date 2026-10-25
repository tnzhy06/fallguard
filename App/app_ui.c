#include "app_ui.h"
#include "bsp_oled.h"
#include "bsp_gps.h"
#include "stm32f1xx_hal.h"
#include <stdio.h>

static uint8_t page;
static uint8_t alarm_active;
static uint8_t dirty;
static uint32_t last_refresh;
static bsp_oled_state_t last_oled_state;

void app_ui_init(void)
{
    page = 0U;
    alarm_active = 0U;
    dirty = 1U;
    last_refresh = HAL_GetTick();
    last_oled_state = BSP_OLED_WAIT_POWER;
    BSP_OLED_Init();
}

void app_ui_next_page(void)
{
    page = (uint8_t)((page + 1U) % 5U);
    dirty = 1U;
}

void app_ui_set_alarm(uint8_t active)
{
    alarm_active = active;
    dirty = 1U;
}

static void draw_page(uint32_t now)
{
    char line[22];

    BSP_OLED_Clear();
    if (alarm_active != 0U)
    {
        BSP_OLED_ShowString(24U, 0U, "TEST ALARM", BSP_OLED_8X16);
        BSP_OLED_DrawLine(0U, 19U, 127U, 19U);
        BSP_OLED_ShowString(16U, 28U, "K2 TO CANCEL", BSP_OLED_8X16);
        BSP_OLED_ShowString(19U, 52U, "LOCAL TEST ONLY", BSP_OLED_6X8);
        BSP_OLED_Reverse();
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
            printf("[C05] OLED init ACK OK @0x3C\r\n");
        }
        else if (state == BSP_OLED_ERROR)
        {
            printf("[C05] OLED bus/ACK failed; check PB4/PB5, pull-ups and 0x3C\r\n");
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
