#ifndef JQ_TEST_MAIN_H
#define JQ_TEST_MAIN_H
#include "stm32f1xx_hal.h"
extern GPIO_TypeDef mock_port;
#define JQ_SDA_GPIO_Port (&mock_port)
#define JQ_BUSY_GPIO_Port (&mock_port)
#define JQ_SDA_Pin 1U
#define JQ_BUSY_Pin 2U
#endif
