#ifndef LEECH001_MPU6050_H
#define LEECH001_MPU6050_H

#include "stm32f1xx_hal.h"

typedef struct
{
    double Q_angle, Q_bias, R_measure;
    double angle, bias;
    double P[2][2];
} mpu6050_kalman_t;

typedef struct
{
    int16_t accel_raw[3];
    int16_t gyro_raw[3];
    double accel_g[3];
    double gyro_dps[3];
    double temperature_c;
    double angle_x, angle_y;
} mpu6050_data_t;

#define MPU6050_ADDRESS (0x68U << 1)
#define MPU6050_TIMEOUT_MS 5U

uint8_t mpu6050_reset(I2C_HandleTypeDef *i2c);
uint8_t mpu6050_configure(I2C_HandleTypeDef *i2c);
uint8_t mpu6050_read_all(I2C_HandleTypeDef *i2c, mpu6050_data_t *data);
void mpu6050_filter_reset(void);
void mpu6050_filter_update(mpu6050_data_t *data, uint32_t tick);

#endif
