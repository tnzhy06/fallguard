#ifndef RUNTIME_HOST_RTC_H
#define RUNTIME_HOST_RTC_H
#include "stm32f1xx_hal.h"

typedef struct { uint32_t CNTH, CNTL; } RTC_TypeDef;
typedef struct { RTC_TypeDef *Instance; } RTC_HandleTypeDef;
typedef struct { uint8_t Hours, Minutes, Seconds; } RTC_TimeTypeDef;

extern RTC_HandleTypeDef hrtc;
extern uint8_t host_lse_ready, host_rtc_overflow;
#define RTC_BKP_DR1 0U
#define RTC_BKP_DR2 1U
#define RTC_BKP_DR3 2U
#define RTC_BKP_DR4 3U
#define RTC_FORMAT_BIN 0U
#define RTC_FLAG_OW 1U
#define RCC_FLAG_LSERDY 1U
#define RESET 0U
#define READ_REG(reg) (reg)
#define __HAL_RCC_GET_FLAG(flag) (host_lse_ready)
#define __HAL_RTC_OVERFLOW_GET_FLAG(handle, flag) (host_rtc_overflow)

uint32_t HAL_RTCEx_BKUPRead(RTC_HandleTypeDef *rtc, uint32_t reg);
void HAL_RTCEx_BKUPWrite(RTC_HandleTypeDef *rtc, uint32_t reg, uint32_t value);
HAL_StatusTypeDef HAL_RTC_SetTime(RTC_HandleTypeDef *rtc, RTC_TimeTypeDef *time, uint32_t format);
#endif
