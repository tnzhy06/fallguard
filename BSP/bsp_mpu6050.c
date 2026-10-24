#include "bsp_mpu6050.h"
#include "mpu6050.h"
#include "i2c.h"
#include <string.h>

static bsp_mpu6050_state_t state;
static bsp_mpu6050_sample_t sample;
static mpu6050_data_t data;
static uint32_t phase_tick, poll_tick;
static uint8_t start_phase;
static uint16_t calibration_samples;
static int32_t gyro_sum[3];
static double gyro_bias[3];

static int32_t rounded(double value)
{
    return (int32_t)(value + ((value >= 0.0) ? 0.5 : -0.5));
}

void BSP_MPU6050_Init(void)
{
    state = BSP_MPU6050_STARTING;
    phase_tick = HAL_GetTick();
    poll_tick = phase_tick;
    start_phase = 0U;
    calibration_samples = 0U;
    memset(&sample, 0, sizeof(sample));
    memset(&data, 0, sizeof(data));
    memset(gyro_sum, 0, sizeof(gyro_sum));
    memset(gyro_bias, 0, sizeof(gyro_bias));
    mpu6050_filter_reset();
}

uint8_t BSP_MPU6050_Poll(void)
{
    uint32_t now = HAL_GetTick();
    uint8_t result;
    unsigned int i;

    if (state == BSP_MPU6050_ERROR)
        return 0U;
    if (state == BSP_MPU6050_STARTING)
    {
        if ((uint32_t)(now - phase_tick) < ((start_phase == 2U) ? 250U : 100U))
            return 0U;
        if (start_phase == 0U)
        {
            if (mpu6050_reset(&hi2c1) == 0U)
            {
                state = BSP_MPU6050_ERROR;
                return 0U;
            }
        }
        else if (start_phase == 1U)
        {
            if (mpu6050_configure(&hi2c1) == 0U)
            {
                state = BSP_MPU6050_ERROR;
                return 0U;
            }
        }
        else
        {
            state = BSP_MPU6050_CALIBRATING;
            poll_tick = now;
        }
        ++start_phase;
        phase_tick = HAL_GetTick();
        return 0U;
    }

    if ((uint32_t)(now - poll_tick) < 2U)
        return 0U;
    poll_tick = now;
    result = mpu6050_read_all(&hi2c1, &data);
    if (result == 2U)
    {
        state = BSP_MPU6050_ERROR;
        return 0U;
    }
    if (result == 0U)
        return 0U;
    now = HAL_GetTick();
    if (state == BSP_MPU6050_CALIBRATING)
    {
        for (i = 0U; i < 3U; ++i)
            gyro_sum[i] += data.gyro_raw[i];
        if (++calibration_samples == 200U)
        {
            for (i = 0U; i < 3U; ++i)
            {
                gyro_bias[i] = (double)gyro_sum[i] / (200.0 * 131.0);
                sample.bias_mdps[i] = rounded(gyro_bias[i] * 1000.0);
            }
            state = BSP_MPU6050_READY;
            mpu6050_filter_reset();
        }
        return 0U;
    }

    for (i = 0U; i < 3U; ++i)
    {
        data.gyro_dps[i] -= gyro_bias[i];
        sample.accel_mg[i] = rounded(data.accel_g[i] * 1000.0);
        sample.gyro_mdps[i] = rounded(data.gyro_dps[i] * 1000.0);
    }
    mpu6050_filter_update(&data, now);
    sample.angle_cdeg[0] = rounded(data.angle_x * 100.0);
    sample.angle_cdeg[1] = rounded(data.angle_y * 100.0);
    sample.temperature_cdeg = rounded(data.temperature_c * 100.0);
    sample.interval_ms = (sample.samples != 0U) ? (uint32_t)(now - sample.tick) : 0U;
    if (sample.interval_ms > sample.max_interval_ms)
        sample.max_interval_ms = sample.interval_ms;
    sample.tick = now;
    ++sample.samples;
    return 1U;
}

bsp_mpu6050_state_t BSP_MPU6050_GetState(void)
{
    return state;
}

const bsp_mpu6050_sample_t *BSP_MPU6050_GetSample(void)
{
    return &sample;
}
