#ifndef RUNTIME_HOST_HAL_H
#define RUNTIME_HOST_HAL_H

#include <stdint.h>

typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef enum { GPIO_PIN_RESET, GPIO_PIN_SET } GPIO_PinState;
typedef struct { uint32_t Pin, Mode, Pull, Speed; } GPIO_InitTypeDef;
typedef struct { uint32_t unused; } I2C_HandleTypeDef;
typedef struct { uint32_t gState; } UART_HandleTypeDef;

#define GPIOB ((void *)0x40010C00)
#define GPIOA ((void *)0x40010800)
#define GPIO_PIN_1 (1U << 1)
#define GPIO_PIN_6 (1U << 6)
#define GPIO_PIN_7 (1U << 7)
#define GPIO_MODE_OUTPUT_OD 1U
#define GPIO_NOPULL 0U
#define GPIO_SPEED_FREQ_HIGH 3U
#define HAL_UART_STATE_READY 0U
#define HAL_UART_STATE_BUSY_TX 1U
#define I2C_MEMADD_SIZE_8BIT 1U
#define I2C_FLAG_BUSY 1U
#define RESET 0U
#define __HAL_GPIO_EXTI_CLEAR_IT(pin) host_clear_exti(pin)
#define __HAL_I2C_GET_FLAG(handle, flag) (host_i2c_busy)
extern uint8_t host_i2c_busy;

uint32_t HAL_GetTick(void);
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t mask);
void host_clear_exti(uint16_t pin);
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *i2c);
HAL_StatusTypeDef HAL_I2C_DeInit(I2C_HandleTypeDef *i2c);
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *i2c, uint16_t address, uint16_t reg,
                                uint16_t address_size, uint8_t *bytes, uint16_t size, uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *i2c, uint16_t address, uint16_t reg,
                                 uint16_t address_size, uint8_t *bytes, uint16_t size, uint32_t timeout);
uint32_t HAL_I2C_GetError(I2C_HandleTypeDef *i2c);
void HAL_GPIO_Init(void *port, GPIO_InitTypeDef *pins);
void HAL_GPIO_WritePin(void *port, uint16_t pins, GPIO_PinState level);
GPIO_PinState HAL_GPIO_ReadPin(void *port, uint16_t pin);
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *uart, const uint8_t *bytes, uint16_t size);
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *uart);

#endif
