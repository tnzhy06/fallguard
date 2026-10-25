#include "bsp_rtc.h"
#include "rtc.h"
#include <string.h>

#define RTC_BACKUP_MAGIC 0xC808U
#define RTC_LOCAL_OFFSET 28800UL

static bsp_rtc_data_t data;
static uint32_t base_utc;

static uint8_t month_days(uint16_t year, uint8_t month)
{
    static const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return (uint8_t)(days[month-1U] + ((month == 2U) &&
           ((year % 4U) == 0U) && (((year % 100U) != 0U) || ((year % 400U) == 0U))));
}

uint32_t BSP_RTC_ToUnixUtc(const bsp_rtc_datetime_t *local)
{
    uint32_t days = 0U;

    if ((local->year < 2000U) || (local->year > 2099U) ||
        (local->month < 1U) || (local->month > 12U) || (local->day < 1U) ||
        (local->day > month_days(local->year, local->month)) ||
        (local->hour > 23U) || (local->minute > 59U) || (local->second > 59U))
        return 0U;
    for (uint16_t y = 1970U; y < local->year; ++y)
        days += 365U + (((y % 4U) == 0U) && (((y % 100U) != 0U) || ((y % 400U) == 0U)));
    for (uint8_t m = 1U; m < local->month; ++m)
        days += month_days(local->year, m);
    days += local->day - 1U;
    return days * 86400UL + local->hour * 3600UL + local->minute * 60UL +
           local->second - RTC_LOCAL_OFFSET;
}

static uint32_t read_counter(void)
{
    uint32_t high = READ_REG(hrtc.Instance->CNTH);
    uint32_t low = READ_REG(hrtc.Instance->CNTL);
    uint32_t again = READ_REG(hrtc.Instance->CNTH);

    if (high != again)
        low = READ_REG(hrtc.Instance->CNTL);
    return (again << 16U) | low;
}

uint8_t BSP_RTC_HasBackup(void)
{
    uint32_t low = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR2);
    uint32_t high = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR3);
    uint32_t base = (high << 16U) | low;

    return (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR1) == RTC_BACKUP_MAGIC) &&
           (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR4) == (low ^ high ^ RTC_BACKUP_MAGIC)) &&
           (base >= 946656000UL) && (base <= 4102329600UL) &&
           (__HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY) != RESET) &&
           !__HAL_RTC_OVERFLOW_GET_FLAG(&hrtc, RTC_FLAG_OW);
}

void BSP_RTC_Init(void)
{
    memset(&data, 0, sizeof(data));
    if (BSP_RTC_HasBackup())
    {
        base_utc = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR2) |
                   (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR3) << 16U);
        data.valid = data.restored = 1U;
        BSP_RTC_Poll();
    }
}

void BSP_RTC_Poll(void)
{
    uint32_t elapsed, local_seconds, days;
    uint16_t year = 1970U;
    uint8_t month = 1U;

    if (!data.valid)
        return;
    elapsed = read_counter();
    if (__HAL_RTC_OVERFLOW_GET_FLAG(&hrtc, RTC_FLAG_OW) ||
        (__HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY) == RESET) || (elapsed > UINT32_MAX - base_utc))
    {
        data.valid = 0U;
        data.unix_utc = 0U;
        data.error = 1U;
        return;
    }
    data.unix_utc = base_utc + elapsed;
    if (data.unix_utc > 4102415999UL)
    {
        data.valid = 0U;
        data.unix_utc = 0U;
        data.error = 1U;
        return;
    }
    local_seconds = data.unix_utc + RTC_LOCAL_OFFSET;
    days = local_seconds / 86400UL;
    while (days >= (uint32_t)(365U + ((year % 4U == 0U) &&
           ((year % 100U != 0U) || (year % 400U == 0U)))))
    {
        days -= 365U + ((year % 4U == 0U) && ((year % 100U != 0U) || (year % 400U == 0U)));
        ++year;
    }
    while (days >= month_days(year, month))
        days -= month_days(year, month++);
    data.local.year = year;
    data.local.month = month;
    data.local.day = (uint8_t)(days + 1U);
    data.local.hour = (uint8_t)((local_seconds % 86400UL) / 3600UL);
    data.local.minute = (uint8_t)((local_seconds % 3600UL) / 60UL);
    data.local.second = (uint8_t)(local_seconds % 60UL);
}

uint8_t BSP_RTC_Set(const bsp_rtc_datetime_t *local)
{
    uint32_t utc = BSP_RTC_ToUnixUtc(local);
    RTC_TimeTypeDef time = {0};

    if (!utc || (__HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY) == RESET))
        return 0U;
    time.Hours = local->hour;
    time.Minutes = local->minute;
    time.Seconds = local->second;
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR1, 0U);
    data.valid = 0U;
    data.unix_utc = 0U;
    if (HAL_RTC_SetTime(&hrtc, &time, RTC_FORMAT_BIN) != HAL_OK)
    {
        data.error = 1U;
        return 0U;
    }
    /* F1 HAL keeps its date in RAM and GetTime wraps the counter each day.
       Keep a persistent midnight epoch instead, so reset/VBAT survival needs no daily writes. */
    base_utc = utc - local->hour * 3600UL - local->minute * 60UL - local->second;
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR2, base_utc & 0xFFFFU);
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR3, base_utc >> 16U);
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR4,
                       (base_utc & 0xFFFFU) ^ (base_utc >> 16U) ^ RTC_BACKUP_MAGIC);
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR1, RTC_BACKUP_MAGIC);
    data.error = data.restored = 0U;
    data.valid = 1U;
    BSP_RTC_Poll();
    return data.valid;
}

const bsp_rtc_data_t *BSP_RTC_GetData(void)
{
    return &data;
}
