/*
 * mpu6050.c
 *
 *  Created on: Nov 13, 2019
 *      Author: Bulanov Konstantin
 *
 *  Contact information
 *  -------------------
 *
 * e-mail   :  leech001@gmail.com
 */

/*
 * |---------------------------------------------------------------------------------
 * | Copyright (C) Bulanov Konstantin,2021
 * |
 * | This program is free software: you can redistribute it and/or modify
 * | it under the terms of the GNU General Public License as published by
 * | the Free Software Foundation, either version 3 of the License, or
 * | any later version.
 * |
 * | This program is distributed in the hope that it will be useful,
 * | but WITHOUT ANY WARRANTY; without even the implied warranty of
 * | MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * | GNU General Public License for more details.
 * |
 * | You should have received a copy of the GNU General Public License
 * | along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * |
 * | Kalman filter algorithm used from https://github.com/TKJElectronics/KalmanFilter
 * |---------------------------------------------------------------------------------
 */

#include "mpu6050.h"
#include "bsp_log.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Fallguard INT-driven STM32F1 port, 2026-11-14.
   Upstream leech001/MPU6050: 37856b26bedd32e04d4cead2d6c028125d4a3ba7. */
#define RAD_TO_DEG 57.2957795130823208768
#define MPU6050_REG_INT_PIN_CFG 0x37U
#define MPU6050_REG_INT_ENABLE  0x38U
#define MPU6050_REG_INT_STATUS  0x3AU
#define MPU6050_INT_LATCHED     0x20U
#define MPU6050_INT_DATA_READY  0x01U

static mpu6050_kalman_t kalman_x, kalman_y;
static uint32_t last_tick;
static uint8_t filter_seeded;

static double mpu6050_kalman_angle(mpu6050_kalman_t *Kalman,
                                  double newAngle, double newRate, double dt);

static uint8_t i2c_ok(I2C_HandleTypeDef *i2c, HAL_StatusTypeDef status,
                      char operation, uint8_t reg)
{
    uint32_t error;
    unsigned int busy;

    if (status == HAL_OK)
        return 1U;
    /* Snapshot the failed transaction before any subsequent bus access. */
    error = HAL_I2C_GetError(i2c);
    busy = (__HAL_I2C_GET_FLAG(i2c, I2C_FLAG_BUSY) != RESET) ? 1U : 0U;
    DEBUG_PRINT(DEBUG_LOG_ERRORS, "[MPU] %c reg=0x%02X HAL=%u err=0x%08lX BUSY=%u\r\n",
           operation, (unsigned int)reg, (unsigned int)status,
           (unsigned long)error, busy);
    return 0U;
}

uint8_t mpu6050_reset(I2C_HandleTypeDef *i2c)
{
    uint8_t value = 0x80U;
    HAL_StatusTypeDef status = HAL_I2C_Mem_Write(i2c, MPU6050_ADDRESS, 0x6B,
        I2C_MEMADD_SIZE_8BIT, &value, 1U, MPU6050_TIMEOUT_MS);

    if (i2c_ok(i2c, status, 'W', 0x6B) == 0U)
    {
        DEBUG_PRINT(DEBUG_LOG_ERRORS, "[MPU] failed at device reset\r\n");
        return 0U;
    }
    return 1U;
}

uint8_t mpu6050_configure(I2C_HandleTypeDef *i2c)
{
    static const uint8_t registers[][2] = {
        {MPU6050_REG_INT_ENABLE, 0x00},
        {0x6B, 0x01}, {0x6C, 0x00},
        {0x19, MPU6050_SAMPLE_RATE_DIV}, {0x1A, MPU6050_DLPF_CFG}, {0x1B, 0x00}, {0x1C, 0x00},
        {MPU6050_REG_INT_PIN_CFG, MPU6050_INT_LATCHED}
    };
    static const uint8_t expected[4] = {MPU6050_SAMPLE_RATE_DIV, MPU6050_DLPF_CFG, 0U, 0U};
    uint8_t check[4], value;
    unsigned int i;
    HAL_StatusTypeDef status;

    status = HAL_I2C_Mem_Read(i2c, MPU6050_ADDRESS, 0x75, I2C_MEMADD_SIZE_8BIT,
                              check, 1U, MPU6050_TIMEOUT_MS);
    if (i2c_ok(i2c, status, 'R', 0x75) == 0U)
        return 0U;
    if (check[0] != 0x68U)
    {
        DEBUG_PRINT(DEBUG_LOG_ERRORS, "[MPU] WHO_AM_I=0x%02X expected=0x68\r\n", (unsigned int)check[0]);
        return 0U;
    }

    for (i = 0U; i < sizeof(registers) / sizeof(registers[0]); ++i)
    {
        value = registers[i][1];
        status = HAL_I2C_Mem_Write(i2c, MPU6050_ADDRESS, registers[i][0],
                                   I2C_MEMADD_SIZE_8BIT, &value, 1U,
                                   MPU6050_TIMEOUT_MS);
        if (i2c_ok(i2c, status, 'W', registers[i][0]) == 0U)
            return 0U;
    }

    /* Read back range and rate before using the matching scale factors. */
    status = HAL_I2C_Mem_Read(i2c, MPU6050_ADDRESS, 0x19, I2C_MEMADD_SIZE_8BIT,
                              check, 4U, MPU6050_TIMEOUT_MS);
    if (i2c_ok(i2c, status, 'R', 0x19) == 0U)
        return 0U;
    for (i = 0U; i < 4U; ++i)
    {
        if (check[i] != expected[i])
        {
            DEBUG_PRINT(DEBUG_LOG_ERRORS, "[MPU] verify reg=0x%02X got=0x%02X expected=0x%02X\r\n",
                   (unsigned int)(0x19U + i), (unsigned int)check[i],
                   (unsigned int)expected[i]);
            return 0U;
        }
    }
    status = HAL_I2C_Mem_Read(i2c, MPU6050_ADDRESS, MPU6050_REG_INT_PIN_CFG,
                              I2C_MEMADD_SIZE_8BIT, check, 2U, MPU6050_TIMEOUT_MS);
    if (i2c_ok(i2c, status, 'R', MPU6050_REG_INT_PIN_CFG) == 0U)
        return 0U;
    if ((check[0] != MPU6050_INT_LATCHED) || (check[1] != 0U))
    {
        DEBUG_PRINT(DEBUG_LOG_ERRORS, "[MPU] INT config mismatch: pin=0x%02X enable=0x%02X\r\n",
                    (unsigned int)check[0], (unsigned int)check[1]);
        return 0U;
    }
    return 1U;
}

uint8_t mpu6050_enable_data_ready(I2C_HandleTypeDef *i2c)
{
    uint8_t value;
    HAL_StatusTypeDef status;

    /* Clear a stale latch before enabling the next DATA_RDY rising edge. */
    status = HAL_I2C_Mem_Read(i2c, MPU6050_ADDRESS, MPU6050_REG_INT_STATUS,
                              I2C_MEMADD_SIZE_8BIT, &value, 1U, MPU6050_TIMEOUT_MS);
    if (i2c_ok(i2c, status, 'R', MPU6050_REG_INT_STATUS) == 0U)
        return 0U;
    value = MPU6050_INT_DATA_READY;
    status = HAL_I2C_Mem_Write(i2c, MPU6050_ADDRESS, MPU6050_REG_INT_ENABLE,
                               I2C_MEMADD_SIZE_8BIT, &value, 1U, MPU6050_TIMEOUT_MS);
    if (i2c_ok(i2c, status, 'W', MPU6050_REG_INT_ENABLE) == 0U)
        return 0U;
    status = HAL_I2C_Mem_Read(i2c, MPU6050_ADDRESS, MPU6050_REG_INT_ENABLE,
                              I2C_MEMADD_SIZE_8BIT, &value, 1U, MPU6050_TIMEOUT_MS);
    if (i2c_ok(i2c, status, 'R', MPU6050_REG_INT_ENABLE) == 0U)
        return 0U;
    if (value != MPU6050_INT_DATA_READY)
    {
        DEBUG_PRINT(DEBUG_LOG_ERRORS, "[MPU] DATA_RDY enable readback=0x%02X\r\n", (unsigned int)value);
        return 0U;
    }
    return 1U;
}

uint8_t mpu6050_read_all(I2C_HandleTypeDef *i2c, mpu6050_data_t *data)
{
    uint8_t bytes[15];
    int16_t temperature;
    unsigned int i;
    HAL_StatusTypeDef status;

    /* Reading INT_STATUS releases the latch. The remaining 14 bytes are sensor data. */
    status = HAL_I2C_Mem_Read(i2c, MPU6050_ADDRESS, MPU6050_REG_INT_STATUS,
                              I2C_MEMADD_SIZE_8BIT, bytes, sizeof(bytes), MPU6050_TIMEOUT_MS);
    if (i2c_ok(i2c, status, 'R', MPU6050_REG_INT_STATUS) == 0U)
        return 2U;
    if ((bytes[0] & MPU6050_INT_DATA_READY) == 0U)
        return 0U;

    for (i = 0U; i < 3U; ++i)
    {
        data->accel_raw[i] = (int16_t)((uint16_t)bytes[1U+2U*i] << 8 | bytes[2U+2U*i]);
        data->gyro_raw[i] = (int16_t)((uint16_t)bytes[9U+2U*i] << 8 | bytes[10U+2U*i]);
        /* The upstream Z correction belongs to its board, not this device. */
        data->accel_g[i] = data->accel_raw[i] / 16384.0;
        data->gyro_dps[i] = data->gyro_raw[i] / 131.0;
    }
    temperature = (int16_t)((uint16_t)bytes[7] << 8 | bytes[8]);
    data->temperature_c = temperature / 340.0 + 36.53;
    return 1U;
}

void mpu6050_filter_reset(void)
{
    memset(&kalman_x, 0, sizeof(kalman_x));
    kalman_x.Q_angle = MPU6050_KALMAN_Q_ANGLE;
    kalman_x.Q_bias = MPU6050_KALMAN_Q_BIAS;
    kalman_x.R_measure = MPU6050_KALMAN_R_MEASURE;
    kalman_y = kalman_x;
    filter_seeded = 0U;
}

void mpu6050_filter_update(mpu6050_data_t *data, uint32_t tick)
{
    double roll, pitch, dt, rate_x;
    double ax = data->accel_g[0], ay = data->accel_g[1], az = data->accel_g[2];
    double roll_sqrt = sqrt(ax * ax + az * az);

    /* atan2 also gives the correct +/-90 degrees when X and Z are zero. */
    roll = atan2(ay, roll_sqrt) * RAD_TO_DEG;
    pitch = atan2(-ax, az) * RAD_TO_DEG;
    dt = (double)(uint32_t)(tick - last_tick) / 1000.0;
    last_tick = tick;
    if (filter_seeded == 0U)
    {
        /* Seed from gravity so boot and calibration time are not integrated. */
        kalman_x.angle = roll;
        kalman_y.angle = pitch;
        data->angle_x = roll;
        data->angle_y = pitch;
        filter_seeded = 1U;
        return;
    }

    if ((pitch < -90.0 && data->angle_y > 90.0) ||
        (pitch > 90.0 && data->angle_y < -90.0))
    {
        kalman_y.angle = pitch;
        data->angle_y = pitch;
    }
    else
        data->angle_y = mpu6050_kalman_angle(&kalman_y, pitch, data->gyro_dps[1], dt);

    /* Preserve the physical X rate when the restricted-roll solution flips it. */
    rate_x = (fabs(data->angle_y) > 90.0) ? -data->gyro_dps[0] : data->gyro_dps[0];
    data->angle_x = mpu6050_kalman_angle(&kalman_x, roll, rate_x, dt);
}

static double mpu6050_kalman_angle(mpu6050_kalman_t *Kalman, double newAngle, double newRate, double dt)
{
    double rate = newRate - Kalman->bias;
    Kalman->angle += dt * rate;

    Kalman->P[0][0] += dt * (dt * Kalman->P[1][1] - Kalman->P[0][1] - Kalman->P[1][0] + Kalman->Q_angle);
    Kalman->P[0][1] -= dt * Kalman->P[1][1];
    Kalman->P[1][0] -= dt * Kalman->P[1][1];
    Kalman->P[1][1] += Kalman->Q_bias * dt;

    double S = Kalman->P[0][0] + Kalman->R_measure;
    double K[2];
    K[0] = Kalman->P[0][0] / S;
    K[1] = Kalman->P[1][0] / S;

    double y = newAngle - Kalman->angle;
    Kalman->angle += K[0] * y;
    Kalman->bias += K[1] * y;

    double P00_temp = Kalman->P[0][0];
    double P01_temp = Kalman->P[0][1];

    Kalman->P[0][0] -= K[0] * P00_temp;
    Kalman->P[0][1] -= K[0] * P01_temp;
    Kalman->P[1][0] -= K[1] * P00_temp;
    Kalman->P[1][1] -= K[1] * P01_temp;

    return Kalman->angle;
}
