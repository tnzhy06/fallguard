#include "app_ui.h"
#include "app_ui_font.h"
#include "app_fall.h"
#include "app_report.h"
#include "debug_config.h"
#include "bsp_oled.h"
#include "bsp_gps.h"
#include "bsp_mpu6050.h"
#include "bsp_rtc.h"
#include "bsp_esp.h"
#include "stm32f1xx_hal.h"
#include <stdio.h>
#include <string.h>

#define UI_PAGE_COUNT 5U

static uint8_t page, alarm_active, dirty;
static uint32_t last_refresh;
static bsp_oled_state_t last_oled_state;

void app_ui_init(void)
{
    page = alarm_active = 0U;
    dirty = 1U;
    last_refresh = HAL_GetTick();
    last_oled_state = BSP_OLED_WAIT_POWER;
    BSP_OLED_Init();
}

void app_ui_next_page(void)
{
    page = (uint8_t)((page + 1U) % UI_PAGE_COUNT);
    dirty = 1U;
}

void app_ui_set_alarm(uint8_t active)
{
    if (alarm_active != active)
    {
        alarm_active = active;
        if (!active)
            page = 0U;
        dirty = 1U;
    }
}

static void draw_chinese(uint8_t x, uint8_t y, const char *text)
{
    while (text[0] && text[1] && text[2] && (x <= 112U))
    {
        size_t i;
        for (i = 0U; i < sizeof(ui_glyphs) / sizeof(ui_glyphs[0]); ++i)
            if (memcmp(text, ui_glyphs[i].code, 3U) == 0)
                break;
        if (i < sizeof(ui_glyphs) / sizeof(ui_glyphs[0]))
            BSP_OLED_ShowImage(x, y, 16U, 16U, ui_glyphs[i].bitmap);
        else
            BSP_OLED_ShowChar((uint8_t)(x + 4U), y, '?', BSP_OLED_8X16);
        x = (uint8_t)(x + 16U);
        text += 3U;
    }
}

static void draw_header(const char *title)
{
    const bsp_esp_data_t *esp = BSP_ESP_GetData();
    const bsp_gps_data_t *gps = BSP_GPS_GetData();
    uint8_t online = BSP_ESP_IsOnline();

    BSP_OLED_ShowString(0U, 0U, title, BSP_OLED_6X8);
    BSP_OLED_DrawLine(82U, 2U, 86U, 0U);
    BSP_OLED_DrawLine(86U, 0U, 90U, 2U);
    BSP_OLED_DrawLine(84U, 4U, 86U, 3U);
    BSP_OLED_DrawLine(86U, 3U, 88U, 4U);
    BSP_OLED_DrawPoint(86, 7);
    if (!esp->wifi_connected)
        BSP_OLED_DrawLine(82U, 0U, 90U, 8U);
    BSP_OLED_DrawCircle(99U, 4U, 3U, online ? BSP_OLED_FILLED : BSP_OLED_UNFILLED);
    BSP_OLED_DrawCircle(104U, 3U, 3U, online ? BSP_OLED_FILLED : BSP_OLED_UNFILLED);
    BSP_OLED_DrawCircle(108U, 5U, 2U, online ? BSP_OLED_FILLED : BSP_OLED_UNFILLED);
    /* Join the circle lobes so the solid cloud has no gap in its base. */
    if (online)
        BSP_OLED_DrawRectangle(99U, 4U, 10U, 3U, BSP_OLED_FILLED);
    BSP_OLED_DrawLine(98U, 7U, 108U, 7U);
    for (uint8_t i = 0U; i < 3U; ++i)
    {
        uint8_t height = (uint8_t)(4U + 2U * i);
        uint8_t filled = gps->signal_valid && (gps->signal_level > i);
        BSP_OLED_DrawRectangle((uint8_t)(115U + 4U * i), (uint8_t)(8U - height),
                               3U, height, filled ? BSP_OLED_FILLED : BSP_OLED_UNFILLED);
    }
    BSP_OLED_DrawLine(0U, 11U, 127U, 11U);
}

static void draw_footer(void)
{
    BSP_OLED_ShowString(0U, 56U, "K1 >", BSP_OLED_6X8);
    BSP_OLED_ShowString(82U, 56U, "K2 TEST", BSP_OLED_6X8);
    for (uint8_t i = 0U; i < UI_PAGE_COUNT; ++i)
        if (page == i)
            BSP_OLED_DrawRectangle((uint8_t)(53U + 6U * i), 59U, 5U, 3U, BSP_OLED_FILLED);
        else
            BSP_OLED_DrawPoint((int16_t)(55U + 6U * i), 60);
}

static const char *fall_status(void)
{
    bsp_mpu6050_state_t state = BSP_MPU6050_GetState();
    app_fall_state_t fall = app_fall_get_data()->state;
    if (state == BSP_MPU6050_ERROR)
        return "故障";
    if (state == BSP_MPU6050_CALIBRATING)
        return "校准";
    if ((state != BSP_MPU6050_READY) || !BSP_MPU6050_GetSample()->samples)
        return "启动";
    if (fall == APP_FALL_WAIT_UPRIGHT)
        return "扶正";
    if (fall == APP_FALL_HOLDING)
        return "倾斜";
    return "正常";
}

static void draw_home(void)
{
    const bsp_rtc_data_t *rtc = BSP_RTC_GetData();
    const bsp_gps_data_t *gps = BSP_GPS_GetData();
    char text[24];

    draw_header("FALLGUARD");
    if (rtc->valid)
        snprintf(text, sizeof(text), "%02u:%02u:%02u", (unsigned int)rtc->local.hour,
                 (unsigned int)rtc->local.minute, (unsigned int)rtc->local.second);
    else
        snprintf(text, sizeof(text), "--:--:--");
    BSP_OLED_ShowString(0U, 16U, text, BSP_OLED_8X16);
    BSP_OLED_DrawRectangle(86U, 14U, 40U, 20U, BSP_OLED_UNFILLED);
    draw_chinese(90U, 16U, fall_status());
    if (rtc->valid)
        snprintf(text, sizeof(text), "%04u-%02u-%02u", (unsigned int)rtc->local.year,
                 (unsigned int)rtc->local.month, (unsigned int)rtc->local.day);
    else
        snprintf(text, sizeof(text), "TIME NOT SYNCED");
    BSP_OLED_ShowString(0U, 36U, text, BSP_OLED_6X8);
    BSP_OLED_DrawCircle(3U, 48U, 2U, BSP_OLED_UNFILLED);
    BSP_OLED_DrawLine(1U, 50U, 3U, 53U);
    BSP_OLED_DrawLine(3U, 53U, 5U, 50U);
    BSP_OLED_ShowString(12U, 46U, gps->fix ? "GPS FIX" : (gps->has_position ? "GPS LAST" : "GPS WAIT"),
                        BSP_OLED_6X8);
}

static void draw_position(void)
{
    const bsp_gps_data_t *gps = BSP_GPS_GetData();
    char text[24];

    draw_header(gps->fix ? "GPS FIX" : (gps->has_position ? "GPS LAST" : "GPS WAIT"));
    for (uint8_t i = 0U; i < 2U; ++i)
    {
        int32_t value = i ? gps->longitude_udeg : gps->latitude_udeg;
        long magnitude = value < 0 ? -(long)value : (long)value;
        char direction = i ? (value < 0 ? 'W' : 'E') : (value < 0 ? 'S' : 'N');
        if (gps->has_position)
        {
            BSP_OLED_ShowChar(0U, (uint8_t)(16U + 19U * i), direction, BSP_OLED_8X16);
            snprintf(text, sizeof(text), "%ld.%06ld", magnitude / 1000000L, magnitude % 1000000L);
        }
        else
        {
            BSP_OLED_ShowChar(0U, (uint8_t)(16U + 19U * i), i ? 'E' : 'N', BSP_OLED_8X16);
            snprintf(text, sizeof(text), "--.------");
        }
        BSP_OLED_ShowString(24U, (uint8_t)(16U + 19U * i), text, BSP_OLED_8X16);
    }
}

static void draw_gps(void)
{
    const bsp_gps_data_t *gps = BSP_GPS_GetData();
    char text[28];

    draw_header(gps->fix ? "GPS DATA" : (gps->has_position ? "GPS LAST" : "GPS WAIT"));
    if (gps->utc_valid)
        snprintf(text, sizeof(text), "UTC  %02u:%02u:%02u", (unsigned int)gps->utc_hour,
                 (unsigned int)gps->utc_minute, (unsigned int)gps->utc_second);
    else
        snprintf(text, sizeof(text), "UTC  --:--:--");
    BSP_OLED_ShowString(0U, 16U, text, BSP_OLED_6X8);
    if (gps->utc_valid)
        snprintf(text, sizeof(text), "     20%02u-%02u-%02u", (unsigned int)gps->utc_year,
                 (unsigned int)gps->utc_month, (unsigned int)gps->utc_day);
    else
        snprintf(text, sizeof(text), "     ----/--/--");
    BSP_OLED_ShowString(0U, 24U, text, BSP_OLED_6X8);
    if (gps->has_position)
    {
        long value = (long)gps->altitude_cm;
        long magnitude = value < 0 ? -value : value;
        snprintf(text, sizeof(text), "ALT  %c%ld.%02ldm", value < 0 ? '-' : '+',
                 magnitude / 100L, magnitude % 100L);
    }
    else
        snprintf(text, sizeof(text), "ALT  --");
    BSP_OLED_ShowString(0U, 32U, text, BSP_OLED_6X8);
    if (gps->hdop_valid)
        snprintf(text, sizeof(text), "HDOP %lu.%02lu  SAT %u", (unsigned long)(gps->hdop_x100 / 100U),
                 (unsigned long)(gps->hdop_x100 % 100U), (unsigned int)gps->satellites);
    else
        snprintf(text, sizeof(text), "HDOP --    SAT --");
    BSP_OLED_ShowString(0U, 40U, text, BSP_OLED_6X8);
    if (gps->signal_valid)
        snprintf(text, sizeof(text), "C/N0 %u dBHz", (unsigned int)gps->signal_max_dbhz);
    else
        snprintf(text, sizeof(text), "C/N0 --");
    BSP_OLED_ShowString(0U, 48U, text, BSP_OLED_6X8);
}

static void draw_motion(void)
{
    const bsp_mpu6050_sample_t *mpu = BSP_MPU6050_GetSample();
    bsp_mpu6050_state_t state = BSP_MPU6050_GetState();
    char text[24];

    draw_header("MOTION");
    if ((state != BSP_MPU6050_READY) || !mpu->samples)
    {
        draw_chinese(48U, 22U, state == BSP_MPU6050_ERROR ? "故障" :
                     (state == BSP_MPU6050_CALIBRATING ? "校准" : "启动"));
        BSP_OLED_ShowString(25U, 43U, state == BSP_MPU6050_ERROR ? "CHECK MPU6050" : "KEEP STILL 3s",
                            BSP_OLED_6X8);
        return;
    }
    BSP_OLED_ShowString(0U, 15U, "A[g]", BSP_OLED_6X8);
    BSP_OLED_ShowString(66U, 15U, "G[deg/s]", BSP_OLED_6X8);
    for (uint8_t i = 0U; i < 3U; ++i)
    {
        long value = (long)mpu->accel_mg[i], magnitude = value < 0 ? -value : value;
        snprintf(text, sizeof(text), "%c %c%ld.%03ld", 'X' + i, value < 0 ? '-' : '+',
                 magnitude / 1000L, magnitude % 1000L);
        BSP_OLED_ShowString(0U, (uint8_t)(24U + 8U * i), text, BSP_OLED_6X8);
        value = (long)mpu->gyro_mdps[i];
        magnitude = value < 0 ? -value : value;
        snprintf(text, sizeof(text), "%c %c%ld.%02ld", 'X' + i, value < 0 ? '-' : '+',
                 magnitude / 1000L, (magnitude % 1000L) / 10L);
        BSP_OLED_ShowString(66U, (uint8_t)(24U + 8U * i), text, BSP_OLED_6X8);
    }
    for (uint8_t i = 0U; i < 2U; ++i)
    {
        long value = (long)mpu->angle_cdeg[i], magnitude = value < 0 ? -value : value;
        snprintf(text, sizeof(text), "%c %c%ld.%01ld", i ? 'P' : 'R', value < 0 ? '-' : '+',
                 magnitude / 100L, (magnitude % 100L) / 10L);
        BSP_OLED_ShowString((uint8_t)(66U * i), 48U, text, BSP_OLED_6X8);
    }
}

static void draw_network(void)
{
    const bsp_esp_data_t *esp = BSP_ESP_GetData();
    const app_report_data_t *report = app_report_get_data();
    const char *stage;
    char text[28];

    draw_header("NETWORK");
    draw_chinese(0U, 17U, BSP_ESP_IsOnline() ? "在线" : "离线");
    BSP_OLED_ShowString(76U, 19U, "belt01", BSP_OLED_8X16);
    if (BSP_ESP_IsOnline())
        stage = "WiFi OK / MQTT OK";
    else if ((esp->state == BSP_ESP_SNTP_CONFIG) || (esp->state == BSP_ESP_SNTP_QUERY))
        stage = "SYNCING TIME";
    else if (esp->state == BSP_ESP_NO_CONFIG)
        stage = "CHECK LOCAL CONFIG";
    else if (esp->state == BSP_ESP_UNSUPPORTED)
        stage = "AT COMMAND REJECTED";
    else if (esp->state >= BSP_ESP_MQTT_CLEAN)
        stage = "CONNECTING MQTT";
    else if (esp->state == BSP_ESP_BACKOFF)
        stage = "RETRY IN PROGRESS";
    else
        stage = "CONNECTING WiFi";
    BSP_OLED_ShowString(0U, 36U, stage, BSP_OLED_6X8);
    snprintf(text, sizeof(text), "INTV %lus TX %lu%s", (unsigned long)report->interval_seconds,
             (unsigned long)(esp->published > 9999U ? 9999U : esp->published),
             esp->published > 9999U ? "+" : "");
    BSP_OLED_ShowString(0U, 46U, text, BSP_OLED_6X8);
}

static void draw_alarm(void)
{
    draw_header("ALERT");
    BSP_OLED_DrawLine(24U, 17U, 8U, 43U);
    BSP_OLED_DrawLine(8U, 43U, 40U, 43U);
    BSP_OLED_DrawLine(40U, 43U, 24U, 17U);
    BSP_OLED_DrawRectangle(23U, 26U, 3U, 9U, BSP_OLED_FILLED);
    BSP_OLED_DrawRectangle(23U, 38U, 3U, 3U, BSP_OLED_FILLED);
    draw_chinese(56U, 16U, app_fall_get_data()->source == APP_FALL_SOURCE_AUTO ? "跌倒报警" : "测试报警");
    BSP_OLED_ShowString(30U, 48U, "K2", BSP_OLED_8X16);
    draw_chinese(58U, 48U, "消警");
}

static void draw_page(uint32_t now)
{
    (void)now;
    BSP_OLED_Clear();
    if (alarm_active)
    {
        draw_alarm();
        return;
    }
    switch (page)
    {
    case 0U: draw_home(); break;
    case 1U: draw_position(); break;
    case 2U: draw_gps(); break;
    case 3U: draw_motion(); break;
    default: draw_network(); break;
    }
    draw_footer();
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
            DEBUG_PRINT(DEBUG_LOG_STATUS, "[OLED] init ACK OK @0x3C\r\n");
        else if (state == BSP_OLED_ERROR)
            DEBUG_PRINT(DEBUG_LOG_ERRORS, "[OLED] bus/ACK failed; check PB4/PB5 and pull-ups\r\n");
    }
    /* The single framebuffer must not change while its previous transfer is in progress. */
    if ((state == BSP_OLED_READY) && !BSP_OLED_IsBusy() &&
        (dirty || ((uint32_t)(now - last_refresh) >= 100U)))
    {
        draw_page(now);
        BSP_OLED_Update();
        dirty = 0U;
        last_refresh = now;
    }
}
