#ifndef TEST_MAIN_H
#define TEST_MAIN_H
#include "stm32f1xx_hal.h"
extern GPIO_TypeDef mock_port;
#define ESP_EN_GPIO_Port (&mock_port)
#define ESP_EN_Pin 1U
#endif
