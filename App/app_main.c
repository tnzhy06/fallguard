#include "app_main.h"
#include "app_ui.h"
#include "bsp_key.h"
#include "bsp_led.h"
#include "bsp_mpu6050.h"
#include "stm32f1xx_hal.h"
#include <stdio.h>

static uint32_t key_last_tick;
static uint8_t test_alarm;
static bsp_mpu6050_state_t last_mpu_state;
static uint32_t log_tick;
static uint8_t log_phase;

static void mpu_diagnostic_poll(void)
{
    bsp_mpu6050_state_t state = BSP_MPU6050_GetState();
    const bsp_mpu6050_sample_t *s = BSP_MPU6050_GetSample();
    uint32_t now = HAL_GetTick();

    if (state != last_mpu_state)
    {
        last_mpu_state = state;
        if (state == BSP_MPU6050_CALIBRATING)
            printf("[C04] WHO=0x68 cfg OK: 2g/250dps 100Hz DLPF=3; keep STILL\r\n");
        else if (state == BSP_MPU6050_READY)
        {
            printf("[C04] gyro bias mdps=%ld,%ld,%ld\r\n",
                   (long)s->bias_mdps[0], (long)s->bias_mdps[1], (long)s->bias_mdps[2]);
            log_tick = now;
        }
        else if (state == BSP_MPU6050_ERROR)
            printf("[C04] MPU stopped; see diagnostic above\r\n");
        return;
    }
    if ((state != BSP_MPU6050_READY) || (s->samples == 0U) ||
        ((uint32_t)(now - log_tick) < 250U))
        return;
    log_tick = now;
    /* Spread diagnostics across loop passes to limit synchronous UART time. */
    if (log_phase == 0U)
        printf("[C04] A mg=%ld,%ld,%ld\r\n", (long)s->accel_mg[0],
               (long)s->accel_mg[1], (long)s->accel_mg[2]);
    else if (log_phase == 1U)
        printf("[C04] G mdps=%ld,%ld,%ld\r\n", (long)s->gyro_mdps[0],
               (long)s->gyro_mdps[1], (long)s->gyro_mdps[2]);
    else if (log_phase == 2U)
        printf("[C04] K cdeg=%ld,%ld T cdeg=%ld\r\n",
               (long)s->angle_cdeg[0], (long)s->angle_cdeg[1], (long)s->temperature_cdeg);
    else
        printf("[C04] N=%lu dt=%lu max=%lu tick=%lu\r\n", (unsigned long)s->samples,
               (unsigned long)s->interval_ms, (unsigned long)s->max_interval_ms,
               (unsigned long)s->tick);
    log_phase = (uint8_t)((log_phase + 1U) % 4U);
}

void app_init(void)
{
    BSP_LED_Init();
    BSP_KEY_Init();
    app_ui_init();
    BSP_MPU6050_Init();
    last_mpu_state = BSP_MPU6050_STARTING;
    log_tick = HAL_GetTick();
    log_phase = 0U;
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
            printf("[C04] K1: page next\r\n");
        }
        if ((pressed & BSP_KEY2_PRESSED) != 0U)
        {
            test_alarm ^= 1U;
            app_ui_set_alarm(test_alarm);
            printf("[C04] K2: test alarm %s\r\n",
                   test_alarm ? "ON" : "OFF");
        }
    }

    BSP_MPU6050_Poll();
    mpu_diagnostic_poll();
    BSP_LED_SetMode(test_alarm ? BSP_LED_ALARM : BSP_LED_OFFLINE);
    BSP_LED_Poll();
    app_ui_poll();
}
