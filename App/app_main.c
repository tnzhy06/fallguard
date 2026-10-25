#include "app_main.h"
#include "app_ui.h"
#include "bsp_key.h"
#include "bsp_led.h"
#include "bsp_mpu6050.h"
#include "bsp_gps.h"
#include "stm32f1xx_hal.h"
#include <stdio.h>

static uint32_t key_last_tick;
static uint8_t test_alarm;
static bsp_mpu6050_state_t last_mpu_state;
static uint32_t gps_log_tick;
static uint8_t gps_log_phase;
static const char *const signal_level_names[] = {"NONE", "WEAK", "MEDIUM", "STRONG"};

static void mpu_diagnostic_poll(void)
{
    bsp_mpu6050_state_t state = BSP_MPU6050_GetState();
    const bsp_mpu6050_sample_t *s = BSP_MPU6050_GetSample();

    if (state != last_mpu_state)
    {
        last_mpu_state = state;
        if (state == BSP_MPU6050_CALIBRATING)
            printf("[C05] WHO=0x68 cfg OK: 2g/250dps 100Hz DLPF=3; keep STILL\r\n");
        else if (state == BSP_MPU6050_READY)
        {
            printf("[C05] gyro bias mdps=%ld,%ld,%ld\r\n",
                   (long)s->bias_mdps[0], (long)s->bias_mdps[1], (long)s->bias_mdps[2]);
        }
        else if (state == BSP_MPU6050_ERROR)
            printf("[C05] MPU stopped; see diagnostic above\r\n");
        return;
    }
}

static void diagnostic_poll(void)
{
    uint32_t now = HAL_GetTick();
    const bsp_gps_data_t *g = BSP_GPS_GetData();
    const bsp_mpu6050_sample_t *m = BSP_MPU6050_GetSample();

    if ((uint32_t)(now - gps_log_tick) < 250U)
        return;
    gps_log_tick = now;
    /* One short line per slot leaves time for UART reception and IMU sampling. */
    switch (gps_log_phase)
    {
    case 0:
        printf("[C05] GPS fix=%u has=%u sat=%u age_ms=%lu\r\n",
               (unsigned int)g->fix, (unsigned int)g->has_position,
               (unsigned int)g->satellites, (unsigned long)g->age_ms);
        break;
    case 1:
        printf("[C05] GPS lat_udeg=%ld lon_udeg=%ld\r\n",
               (long)g->latitude_udeg, (long)g->longitude_udeg);
        break;
    case 2:
        printf("[C05] GPS UTC=20%02u-%02u-%02u %02u:%02u:%02u valid=%u\r\n",
               (unsigned int)g->utc_year, (unsigned int)g->utc_month,
               (unsigned int)g->utc_day, (unsigned int)g->utc_hour,
               (unsigned int)g->utc_minute, (unsigned int)g->utc_second,
               (unsigned int)g->utc_valid);
        break;
    case 3:
        printf("[C05] GPS alt_cm=%ld hdop_x100=%lu hv=%u\r\n",
               (long)g->altitude_cm,
               (unsigned long)g->hdop_x100, (unsigned int)g->hdop_valid);
        break;
    case 4:
        printf("[C05] GPS rx=%lu gga=%lu rmc=%lu\r\n", (unsigned long)g->rx_bytes,
               (unsigned long)g->gga_count, (unsigned long)g->rmc_count);
        break;
    case 5:
        printf("[C05] GPS crc=%lu bad=%lu drop=%lu uart=%lu err=%08lX\r\n",
               (unsigned long)g->checksum_errors, (unsigned long)g->bad_frames,
               (unsigned long)g->dropped_bytes, (unsigned long)g->uart_errors,
               (unsigned long)g->uart_error_mask);
        break;
    case 6:
        printf("[C05] IMU N=%lu dt=%lu max=%lu tick=%lu\r\n", (unsigned long)m->samples,
               (unsigned long)m->interval_ms, (unsigned long)m->max_interval_ms,
               (unsigned long)m->tick);
        break;
    case 7:
        printf("[C05] A mg=%ld,%ld,%ld\r\n", (long)m->accel_mg[0],
               (long)m->accel_mg[1], (long)m->accel_mg[2]);
        break;
    case 8:
        printf("[C05] G mdps=%ld,%ld,%ld\r\n", (long)m->gyro_mdps[0],
               (long)m->gyro_mdps[1], (long)m->gyro_mdps[2]);
        break;
    case 9:
        printf("[C05] K cdeg=%ld,%ld T cdeg=%ld\r\n", (long)m->angle_cdeg[0],
               (long)m->angle_cdeg[1], (long)m->temperature_cdeg);
        break;
    default:
        printf("[C05] GSV view=%u cn0_n=%u max_dbhz=%u valid=%u gv=%u level=%s\r\n",
               (unsigned int)g->satellites_in_view, (unsigned int)g->signal_count,
               (unsigned int)g->signal_max_dbhz, (unsigned int)g->signal_valid,
               (unsigned int)g->gsv_valid, signal_level_names[g->signal_level]);
        break;
    }
    gps_log_phase = (uint8_t)((gps_log_phase + 1U) % 11U);
}

void app_init(void)
{
    BSP_LED_Init();
    BSP_KEY_Init();
    app_ui_init();
    BSP_MPU6050_Init();
    BSP_GPS_Init();
    last_mpu_state = BSP_MPU6050_STARTING;
    gps_log_tick = HAL_GetTick();
    gps_log_phase = 0U;
    key_last_tick = HAL_GetTick();
    test_alarm = 0U;
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
            printf("[C05] K1: page next\r\n");
        }
        if ((pressed & BSP_KEY2_PRESSED) != 0U)
        {
            test_alarm ^= 1U;
            app_ui_set_alarm(test_alarm);
            printf("[C05] K2: test alarm %s\r\n",
                   test_alarm ? "ON" : "OFF");
        }
    }

    BSP_MPU6050_Poll();
    BSP_GPS_Poll();
    mpu_diagnostic_poll();
    diagnostic_poll();
    BSP_LED_SetMode(test_alarm ? BSP_LED_ALARM : BSP_LED_OFFLINE);
    BSP_LED_Poll();
    app_ui_poll();
}
