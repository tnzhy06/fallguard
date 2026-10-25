#include "app_fall.h"
#include "bsp_mpu6050.h"
#include "stm32f1xx_hal.h"

static app_fall_data_t data;
static uint32_t hold_tick, last_tick, last_sample;
static uint8_t have_sample;

void app_fall_init(void)
{
    data.state = APP_FALL_IDLE;
    data.source = APP_FALL_SOURCE_NONE;
    data.held_ms = 0U;
    have_sample = 0U;
}

void app_fall_toggle_alarm(void)
{
    data.held_ms = 0U;
    if (data.state == APP_FALL_ALARM)
    {
        /* A canceled alarm cannot immediately repeat while the board stays tilted. */
        data.state = APP_FALL_WAIT_UPRIGHT;
        data.source = APP_FALL_SOURCE_NONE;
    }
    else
    {
        data.state = APP_FALL_ALARM;
        data.source = APP_FALL_SOURCE_MANUAL;
    }
}

void app_fall_poll(void)
{
    const bsp_mpu6050_sample_t *sample = BSP_MPU6050_GetSample();
    uint8_t valid = (BSP_MPU6050_GetState() == BSP_MPU6050_READY) && (sample->samples != 0U);
    uint8_t tilted;

    if (!valid || (have_sample && ((uint32_t)(HAL_GetTick() - last_tick) >= APP_FALL_HOLD_MS)))
    {
        if (data.state == APP_FALL_HOLDING)
        {
            data.state = APP_FALL_IDLE;
            data.held_ms = 0U;
        }
        if (!valid)
        {
            have_sample = 0U;
            return;
        }
    }
    if (have_sample && (sample->samples == last_sample))
        return;
    /* A whole hold interval without samples must not be counted as an observed tilt. */
    if (have_sample && ((uint32_t)(sample->tick - last_tick) >= APP_FALL_HOLD_MS) &&
        (data.state == APP_FALL_HOLDING))
    {
        data.state = APP_FALL_IDLE;
        data.held_ms = 0U;
    }
    last_tick = sample->tick;
    last_sample = sample->samples;
    have_sample = 1U;
    tilted = (sample->angle_cdeg[0] >= APP_FALL_ANGLE_DEG * 100) ||
             (sample->angle_cdeg[0] <= -APP_FALL_ANGLE_DEG * 100) ||
             (sample->angle_cdeg[1] >= APP_FALL_ANGLE_DEG * 100) ||
             (sample->angle_cdeg[1] <= -APP_FALL_ANGLE_DEG * 100);

    if (data.state == APP_FALL_ALARM)
        return;
    if (data.state == APP_FALL_WAIT_UPRIGHT)
    {
        if (!tilted)
            data.state = APP_FALL_IDLE;
        return;
    }
    if (!tilted)
    {
        data.state = APP_FALL_IDLE;
        data.held_ms = 0U;
    }
    else if (data.state == APP_FALL_IDLE)
    {
        data.state = APP_FALL_HOLDING;
        hold_tick = sample->tick;
    }
    else
    {
        data.held_ms = (uint32_t)(sample->tick - hold_tick);
        if (data.held_ms >= APP_FALL_HOLD_MS)
        {
            data.held_ms = APP_FALL_HOLD_MS;
            data.state = APP_FALL_ALARM;
            data.source = APP_FALL_SOURCE_AUTO;
        }
    }
}

const app_fall_data_t *app_fall_get_data(void)
{
    return &data;
}
