#ifndef TEST_HAL_H
#define TEST_HAL_H
#include <stdint.h>
typedef enum {HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT} HAL_StatusTypeDef;
typedef enum {GPIO_PIN_RESET, GPIO_PIN_SET} GPIO_PinState;
typedef struct {uint32_t CNTH, CNTL;} RTC_TypeDef;
typedef struct {RTC_TypeDef *Instance;} RTC_HandleTypeDef;
typedef struct {uint8_t Hours, Minutes, Seconds;} RTC_TimeTypeDef;
typedef struct {uint32_t gState;} UART_HandleTypeDef;
typedef struct {int unused;} GPIO_TypeDef;
extern uint32_t mock_lse, mock_overflow;
#define RESET 0U
#define RCC_FLAG_LSERDY 1U
#define RTC_FLAG_OW 1U
#define RTC_FORMAT_BIN 0U
#define RTC_BKP_DR1 1U
#define RTC_BKP_DR2 2U
#define RTC_BKP_DR3 3U
#define RTC_BKP_DR4 4U
#define HAL_UART_STATE_READY 0U
#define HAL_UART_STATE_BUSY_TX 1U
#define READ_REG(reg) (reg)
#define __HAL_RCC_GET_FLAG(flag) (mock_lse)
#define __HAL_RTC_OVERFLOW_GET_FLAG(rtc, flag) (mock_overflow)
uint32_t HAL_GetTick(void);
void HAL_GPIO_WritePin(GPIO_TypeDef *, uint16_t, GPIO_PinState);
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *, uint8_t *, uint16_t);
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *);
HAL_StatusTypeDef HAL_RTC_SetTime(RTC_HandleTypeDef *, RTC_TimeTypeDef *, uint32_t);
uint32_t HAL_RTCEx_BKUPRead(RTC_HandleTypeDef *, uint32_t);
void HAL_RTCEx_BKUPWrite(RTC_HandleTypeDef *, uint32_t, uint32_t);
#endif
