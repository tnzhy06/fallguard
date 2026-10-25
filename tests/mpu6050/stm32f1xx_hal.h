#ifndef MPU_TEST_HAL_H
#define MPU_TEST_HAL_H
#include <stdint.h>
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef struct { uint32_t ErrorCode; } I2C_HandleTypeDef;
#define I2C_MEMADD_SIZE_8BIT 1U
#define I2C_FLAG_BUSY 1U
#define RESET 0U
#define __HAL_I2C_GET_FLAG(handle, flag) 0U
uint32_t HAL_GetTick(void);
uint32_t HAL_I2C_GetError(I2C_HandleTypeDef *handle);
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *, uint16_t, uint16_t, uint16_t, uint8_t *, uint16_t, uint32_t);
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *, uint16_t, uint16_t, uint16_t, uint8_t *, uint16_t, uint32_t);
#endif
