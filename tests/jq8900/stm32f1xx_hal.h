#ifndef JQ_TEST_HAL_H
#define JQ_TEST_HAL_H
#include <stdint.h>
typedef enum { HAL_OK, HAL_ERROR } HAL_StatusTypeDef;
typedef enum { GPIO_PIN_RESET, GPIO_PIN_SET } GPIO_PinState;
typedef struct { uint32_t dummy; } GPIO_TypeDef;
typedef struct { uint32_t arr, counter; } TIM_HandleTypeDef;
#define TIM_FLAG_UPDATE 1U
#define __HAL_TIM_SET_AUTORELOAD(h, value) ((h)->arr = (value))
#define __HAL_TIM_SET_COUNTER(h, value) ((h)->counter = (value))
#define __HAL_TIM_CLEAR_FLAG(h, flag) ((void)(h), (void)(flag))
uint32_t HAL_GetTick(void);
HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *timer);
HAL_StatusTypeDef HAL_TIM_Base_Stop_IT(TIM_HandleTypeDef *timer);
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState level);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);
#endif
