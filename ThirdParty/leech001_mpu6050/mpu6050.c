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
#include <math.h>
#include <stdio.h>
#include <string.h>

#define RAD_TO_DEG 57.2957795130823208768

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
    printf("[C04] MPU %c reg=0x%02X HAL=%u err=0x%08lX BUSY=%u\r\n",
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
        printf("[C04] MPU failed at device reset\r\n");
        return 0U;
    }
    return 1U;
}

uint8_t mpu6050_configure(I2C_HandleTypeDef *i2c)
{
    static const uint8_t registers[][2] = {
        {0x6B, 0x01}, {0x6C, 0x00},
        {0x19, 0x09}, {0x1A, 0x03}, {0x1B, 0x00}, {0x1C, 0x00},
        {0x37, 0x00}, {0x38, 0x01}
    };
    static const uint8_t expected[4] = {9U, 3U, 0U, 0U};
    uint8_t check[4], value;
    unsigned int i;
    HAL_StatusTypeDef status;

    status = HAL_I2C_Mem_Read(i2c, MPU6050_ADDRESS, 0x75, I2C_MEMADD_SIZE_8BIT,
                              check, 1U, MPU6050_TIMEOUT_MS);
    if (i2c_ok(i2c, status, 'R', 0x75) == 0U)
        return 0U;
    if (check[0] != 0x68U)
    {
        printf("[C04] MPU WHO_AM_I=0x%02X expected=0x68\r\n", (unsigned int)check[0]);
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
            printf("[C04] MPU verify reg=0x%02X got=0x%02X expected=0x%02X\r\n",
                   (unsigned int)(0x19U + i), (unsigned int)check[i],
                   (unsigned int)expected[i]);
            return 0U;
        }
    }
    return 1U;
}

uint8_t mpu6050_read_all(I2C_HandleTypeDef *i2c, mpu6050_data_t *data)
{
    uint8_t bytes[14], ready;
    int16_t temperature;
    unsigned int i;
    HAL_StatusTypeDef status;

    status = HAL_I2C_Mem_Read(i2c, MPU6050_ADDRESS, 0x3A, I2C_MEMADD_SIZE_8BIT,
                              &ready, 1U, MPU6050_TIMEOUT_MS);
    if (i2c_ok(i2c, status, 'R', 0x3A) == 0U)
        return 2U;
    if ((ready & 1U) == 0U)
        return 0U;
    status = HAL_I2C_Mem_Read(i2c, MPU6050_ADDRESS, 0x3B, I2C_MEMADD_SIZE_8BIT,
                              bytes, 14U, MPU6050_TIMEOUT_MS);
    if (i2c_ok(i2c, status, 'R', 0x3B) == 0U)
        return 2U;

    for (i = 0U; i < 3U; ++i)
    {
        data->accel_raw[i] = (int16_t)((uint16_t)bytes[2U*i] << 8 | bytes[2U*i+1U]);
        data->gyro_raw[i] = (int16_t)((uint16_t)bytes[8U+2U*i] << 8 | bytes[9U+2U*i]);
        /* The upstream Z correction belongs to its board, not this device. */
        data->accel_g[i] = data->accel_raw[i] / 16384.0;
        data->gyro_dps[i] = data->gyro_raw[i] / 131.0;
    }
    temperature = (int16_t)((uint16_t)bytes[6] << 8 | bytes[7]);
    data->temperature_c = temperature / 340.0 + 36.53;
    return 1U;
}

void mpu6050_filter_reset(void)
{
    memset(&kalman_x, 0, sizeof(kalman_x));
    kalman_x.Q_angle = 0.001;
    kalman_x.Q_bias = 0.003;
    kalman_x.R_measure = 0.03;
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
