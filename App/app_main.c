#include "app_main.h"
#include "debug_config.h"
#include "app_ui.h"
#include "app_fall.h"
#include "app_voice.h"
#include "app_time.h"
#include "bsp_key.h"
#include "bsp_led.h"
#include "bsp_gps.h"
#include "bsp_mpu6050.h"
#include "stm32f1xx_hal.h"
#include <stdio.h>

static uint32_t key_last_tick;
static app_fall_state_t last_fall_state;
static uint32_t gps_log_tick;
static uint8_t gps_log_phase;
static bsp_mpu6050_state_t last_mpu_state;
static bsp_mpu6050_sample_t mpu_log_sample;
static uint32_t mpu_frame_tick, mpu_line_tick;
static uint8_t mpu_log_phase;
static const char *const signal_level_names[] = {"NONE", "WEAK", "MEDIUM", "STRONG"};

static void diagnostic_poll(void)
{
    uint32_t now = HAL_GetTick();
    const bsp_gps_data_t *g = BSP_GPS_GetData();
    static const uint8_t enabled[] = {
        DEBUG_LOG_GPS_FIX, DEBUG_LOG_GPS_POSITION, DEBUG_LOG_GPS_UTC,
        DEBUG_LOG_GPS_ALTITUDE, DEBUG_LOG_GPS_STATS, DEBUG_LOG_GPS_STATS,
        DEBUG_LOG_GPS_SIGNAL
    };
    uint8_t skipped = 0U;

    if (!DEBUG_OUTPUT_ENABLE || !(DEBUG_LOG_GPS_FIX || DEBUG_LOG_GPS_POSITION ||
        DEBUG_LOG_GPS_UTC || DEBUG_LOG_GPS_ALTITUDE || DEBUG_LOG_GPS_STATS || DEBUG_LOG_GPS_SIGNAL))
        return;
    if ((uint32_t)(now - gps_log_tick) < DEBUG_GPS_PERIOD_MS)
        return;
    while ((enabled[gps_log_phase] == 0U) && (skipped < 7U))
    {
        gps_log_phase = (uint8_t)((gps_log_phase + 1U) % 7U);
        ++skipped;
    }
    if (skipped == 7U)
        return;
    gps_log_tick = now;
    /* One short line per slot leaves time for UART reception. */
    switch (gps_log_phase)
    {
    case 0:
        DEBUG_PRINT(DEBUG_LOG_GPS_FIX, "[C05] GPS fix=%u has=%u sat=%u age_ms=%lu\r\n",
               (unsigned int)g->fix, (unsigned int)g->has_position,
               (unsigned int)g->satellites, (unsigned long)g->age_ms);
        break;
    case 1:
        DEBUG_PRINT(DEBUG_LOG_GPS_POSITION, "[C05] GPS lat_udeg=%ld lon_udeg=%ld\r\n",
               (long)g->latitude_udeg, (long)g->longitude_udeg);
        break;
    case 2:
        DEBUG_PRINT(DEBUG_LOG_GPS_UTC, "[C05] GPS UTC=20%02u-%02u-%02u %02u:%02u:%02u valid=%u\r\n",
               (unsigned int)g->utc_year, (unsigned int)g->utc_month,
               (unsigned int)g->utc_day, (unsigned int)g->utc_hour,
               (unsigned int)g->utc_minute, (unsigned int)g->utc_second,
               (unsigned int)g->utc_valid);
        break;
    case 3:
        DEBUG_PRINT(DEBUG_LOG_GPS_ALTITUDE, "[C05] GPS alt_cm=%ld hdop_x100=%lu hv=%u\r\n",
               (long)g->altitude_cm,
               (unsigned long)g->hdop_x100, (unsigned int)g->hdop_valid);
        break;
    case 4:
        DEBUG_PRINT(DEBUG_LOG_GPS_STATS, "[C05] GPS rx=%lu gga=%lu rmc=%lu\r\n", (unsigned long)g->rx_bytes,
               (unsigned long)g->gga_count, (unsigned long)g->rmc_count);
        break;
    case 5:
        DEBUG_PRINT(DEBUG_LOG_GPS_STATS, "[C05] GPS crc=%lu bad=%lu drop=%lu uart=%lu err=%08lX\r\n",
               (unsigned long)g->checksum_errors, (unsigned long)g->bad_frames,
               (unsigned long)g->dropped_bytes, (unsigned long)g->uart_errors,
               (unsigned long)g->uart_error_mask);
        break;
    default:
        DEBUG_PRINT(DEBUG_LOG_GPS_SIGNAL, "[C05] GSV view=%u cn0_n=%u max_dbhz=%u valid=%u gv=%u level=%s\r\n",
               (unsigned int)g->satellites_in_view, (unsigned int)g->signal_count,
               (unsigned int)g->signal_max_dbhz, (unsigned int)g->signal_valid,
               (unsigned int)g->gsv_valid, signal_level_names[g->signal_level]);
        break;
    }
    gps_log_phase = (uint8_t)((gps_log_phase + 1U) % 7U);
}

static uint8_t mpu_display_log_poll(void)
{
    static const uint8_t enabled[] = {
        DEBUG_LOG_MPU_ACCEL, DEBUG_LOG_MPU_GYRO, DEBUG_LOG_MPU_ANGLES
    };
    uint32_t now = HAL_GetTick();
    bsp_mpu6050_state_t state = BSP_MPU6050_GetState();
    const bsp_mpu6050_sample_t *sample = BSP_MPU6050_GetSample();

    if (state != last_mpu_state)
    {
        last_mpu_state = state;
        mpu_log_phase = 0U;
        mpu_frame_tick = now;
        if (state == BSP_MPU6050_CALIBRATING)
            DEBUG_PRINT(DEBUG_LOG_STATUS, "[MPU] WHO=0x68 config OK; keep STILL for gyro zero bias\r\n");
        else if (state == BSP_MPU6050_READY)
            DEBUG_PRINT(DEBUG_LOG_STATUS, "[MPU] gyro bias mdps=%ld,%ld,%ld\r\n", (long)sample->bias_mdps[0],
                        (long)sample->bias_mdps[1], (long)sample->bias_mdps[2]);
        else if (state == BSP_MPU6050_ERROR)
            DEBUG_PRINT(DEBUG_LOG_ERRORS, "[MPU] stopped; check diagnostic, PB6/PB7 and AD0=0\r\n");
        return DEBUG_OUTPUT_ENABLE && ((state == BSP_MPU6050_ERROR) ? DEBUG_LOG_ERRORS : DEBUG_LOG_STATUS);
    }
    if (!DEBUG_OUTPUT_ENABLE || !(DEBUG_LOG_MPU_ACCEL || DEBUG_LOG_MPU_GYRO || DEBUG_LOG_MPU_ANGLES) ||
        (state != BSP_MPU6050_READY) || (sample->samples == 0U))
        return 0U;
    if (mpu_log_phase == 0U)
    {
        if ((uint32_t)(now - mpu_frame_tick) < DEBUG_MPU_PERIOD_MS)
            return 0U;
        mpu_frame_tick = now;
        mpu_log_sample = *sample;
        mpu_log_phase = 1U;
        mpu_line_tick = now - 20U;
    }
    /* Skip disabled fields without consuming their output slots. */
    while ((mpu_log_phase <= 3U) && (enabled[mpu_log_phase - 1U] == 0U))
        ++mpu_log_phase;
    if (mpu_log_phase > 3U)
    {
        mpu_log_phase = 0U;
        return 0U;
    }
    if ((uint32_t)(now - mpu_line_tick) < 20U)
        return 1U;
    mpu_line_tick = now;
    switch (mpu_log_phase)
    {
    case 1:
        DEBUG_PRINT(DEBUG_LOG_MPU_ACCEL, "[MPU] n=%lu A mg=%ld,%ld,%ld\r\n", (unsigned long)mpu_log_sample.samples,
                    (long)mpu_log_sample.accel_mg[0], (long)mpu_log_sample.accel_mg[1],
                    (long)mpu_log_sample.accel_mg[2]);
        break;
    case 2:
        DEBUG_PRINT(DEBUG_LOG_MPU_GYRO, "[MPU] n=%lu G mdps=%ld,%ld,%ld\r\n", (unsigned long)mpu_log_sample.samples,
                    (long)mpu_log_sample.gyro_mdps[0], (long)mpu_log_sample.gyro_mdps[1],
                    (long)mpu_log_sample.gyro_mdps[2]);
        break;
    default:
        DEBUG_PRINT(DEBUG_LOG_MPU_ANGLES, "[MPU] n=%lu Roll=%ld Pitch=%ld cdeg dt=%lu max=%lu\r\n",
                    (unsigned long)mpu_log_sample.samples, (long)mpu_log_sample.angle_cdeg[0],
                    (long)mpu_log_sample.angle_cdeg[1], (unsigned long)mpu_log_sample.interval_ms,
                    (unsigned long)mpu_log_sample.max_interval_ms);
        break;
    }
    ++mpu_log_phase;
    while ((mpu_log_phase <= 3U) && (enabled[mpu_log_phase - 1U] == 0U))
        ++mpu_log_phase;
    if (mpu_log_phase > 3U)
        mpu_log_phase = 0U;
    return 1U;
}

static uint8_t fall_event_log_poll(void)
{
    const app_fall_data_t *fall = app_fall_get_data();
    app_fall_state_t previous = last_fall_state;

    if (fall->state == previous)
        return 0U;
    last_fall_state = fall->state;
    if (fall->state == APP_FALL_ALARM)
        DEBUG_PRINT(DEBUG_LOG_FALL, "[FALL] alarm source=%s; K2 to cancel\r\n",
                    fall->source == APP_FALL_SOURCE_AUTO ? "AUTO" : "MANUAL");
    else if (previous == APP_FALL_ALARM)
        DEBUG_PRINT(DEBUG_LOG_FALL, "[FALL] canceled; %s\r\n",
                    fall->state == APP_FALL_WAIT_UPRIGHT ? "raise board to rearm" : "ready");
    else if ((previous == APP_FALL_WAIT_UPRIGHT) && (fall->state == APP_FALL_IDLE))
        DEBUG_PRINT(DEBUG_LOG_FALL, "[FALL] upright; ready\r\n");
    else
        return 0U;
    return DEBUG_OUTPUT_ENABLE && DEBUG_LOG_FALL;
}

void app_init(void)
{
    BSP_LED_Init();
    BSP_KEY_Init();
    app_ui_init();
    BSP_GPS_Init();
    BSP_MPU6050_Init();
    app_fall_init();
    app_voice_init();
    app_time_init();
    last_fall_state = APP_FALL_IDLE;
    last_mpu_state = BSP_MPU6050_STARTING;
    mpu_log_phase = 0U;
    mpu_frame_tick = mpu_line_tick = HAL_GetTick();
    gps_log_tick = HAL_GetTick();
    gps_log_phase = 0U;
    key_last_tick = HAL_GetTick();
}

void app_run(void)
{
    uint32_t now = HAL_GetTick();

    if ((uint32_t)(now - key_last_tick) >= 5U)
    {
        uint8_t pressed;

        key_last_tick = now;
        pressed = BSP_KEY_Scan();
        if ((pressed & BSP_KEY1_PRESSED) != 0U)
        {
            app_ui_next_page();
            DEBUG_PRINT(DEBUG_LOG_KEYS, "[C05] K1: page next\r\n");
        }
        if ((pressed & BSP_KEY2_PRESSED) != 0U)
        {
            app_fall_toggle_alarm();
            DEBUG_PRINT(DEBUG_LOG_KEYS, "[C05] K2: alarm control\r\n");
        }
    }

    BSP_MPU6050_Poll();
    BSP_GPS_Poll();
    app_time_poll();
    app_fall_poll();
    app_voice_poll(app_fall_get_data()->state == APP_FALL_ALARM);
    app_ui_set_alarm(app_fall_get_data()->state == APP_FALL_ALARM);
    if (!fall_event_log_poll() && !mpu_display_log_poll())
        diagnostic_poll();
    BSP_LED_SetMode(app_fall_get_data()->state == APP_FALL_ALARM ? BSP_LED_ALARM : BSP_LED_OFFLINE);
    BSP_LED_Poll();
    app_ui_poll();
}
