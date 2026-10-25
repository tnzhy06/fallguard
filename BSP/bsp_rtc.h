#ifndef BSP_RTC_H
#define BSP_RTC_H

#include <stdint.h>

typedef struct
{
    uint16_t year;
    uint8_t month, day, hour, minute, second;
} bsp_rtc_datetime_t;

typedef struct
{
    bsp_rtc_datetime_t local;
    uint32_t unix_utc;
    uint8_t valid, restored, error;
} bsp_rtc_data_t;

uint8_t BSP_RTC_HasBackup(void);
void BSP_RTC_Init(void);
void BSP_RTC_Poll(void);
uint8_t BSP_RTC_Set(const bsp_rtc_datetime_t *local);
uint32_t BSP_RTC_ToUnixUtc(const bsp_rtc_datetime_t *local);
const bsp_rtc_data_t *BSP_RTC_GetData(void);

#endif
