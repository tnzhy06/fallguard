#ifndef BSP_MPU6050_H
#define BSP_MPU6050_H

#include <stdint.h>

typedef enum
{
    BSP_MPU6050_STARTING,
    BSP_MPU6050_CALIBRATING,
    BSP_MPU6050_READY,
    BSP_MPU6050_ERROR
} bsp_mpu6050_state_t;

typedef struct
{
    int32_t accel_mg[3];
    int32_t gyro_mdps[3];
    int32_t angle_cdeg[2];
    int32_t temperature_cdeg;
    int32_t bias_mdps[3];
    uint32_t samples;
    uint32_t tick;
    uint32_t interval_ms;
    uint32_t max_interval_ms;
} bsp_mpu6050_sample_t;

void BSP_MPU6050_Init(void);
uint8_t BSP_MPU6050_Poll(void);
bsp_mpu6050_state_t BSP_MPU6050_GetState(void);
const bsp_mpu6050_sample_t *BSP_MPU6050_GetSample(void);

#endif
