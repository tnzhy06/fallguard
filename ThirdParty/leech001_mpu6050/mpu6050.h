#ifndef LEECH001_MPU6050_H
#define LEECH001_MPU6050_H

#include "stm32f1xx_hal.h"
#include "app_config.h"

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

#define MPU6050_ADDRESS (MPU6050_I2C_ADDRESS << 1)
#define MPU6050_FRAME_BYTES 15U

uint8_t mpu6050_reset(I2C_HandleTypeDef *i2c);
uint8_t mpu6050_configure(I2C_HandleTypeDef *i2c);
/* Arm latched, active-high DATA_RDY after startup settling. Returns 1 on success. */
uint8_t mpu6050_enable_data_ready(I2C_HandleTypeDef *i2c);
/* The caller owns bytes until completion/error. HAL_OK means started, not read. */
HAL_StatusTypeDef mpu6050_start_read(I2C_HandleTypeDef *i2c, uint8_t *bytes);
/* Decode only after MemRxCplt. Returns 0 without modifying data for no DATA_RDY. */
uint8_t mpu6050_decode_frame(const uint8_t *bytes, mpu6050_data_t *data);
void mpu6050_filter_reset(void);
void mpu6050_filter_update(mpu6050_data_t *data, uint32_t tick);

#endif
