#include "bsp_rtc.h"
#include "rtc.h"
#include "app_config.h"
#include <string.h>

#define RTC_BACKUP_MAGIC 0xC808U
#define RTC_LOCAL_OFFSET (APP_TIMEZONE_HOURS * 3600L)

static bsp_rtc_data_t data;
static uint32_t base_utc;
static uint8_t hardware_ready;

void BSP_RTC_SetHardwareReady(uint8_t ready)
{
    hardware_ready = ready ? 1U : 0U;
    if (!hardware_ready)
    {
        /* This only invalidates RAM state; backup registers remain untouched. */
        memset(&data, 0, sizeof(data));
        data.error = 1U;
    }
    else
        data.error = 0U;
}

uint8_t BSP_RTC_IsHardwareReady(void)
{
    return hardware_ready && (__HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY) != RESET);
}

uint8_t BSP_RTC_ConfigureClock(void)
{
    RCC_OscInitTypeDef oscillator = {0};
    RCC_PeriphCLKInitTypeDef clock = {0};

    BSP_RTC_SetHardwareReady(0U);
    oscillator.OscillatorType = RCC_OSCILLATORTYPE_LSE;
    oscillator.LSEState = RCC_LSE_ON;
    oscillator.PLL.PLLState = RCC_PLL_NONE;
    /* Keep the HAL startup deadline (LSE_STARTUP_TIMEOUT, normally 5 seconds).
       Do not combine this optional oscillator with HSE/PLL initialization. */
    if (HAL_RCC_OscConfig(&oscillator) != HAL_OK)
        return 0U;
    /* Never change the RTC source/reset the backup domain after an LSE failure. */
    clock.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    clock.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
    return HAL_RCCEx_PeriphCLKConfig(&clock) == HAL_OK;
}

void BSP_RTC_InitHardware(void)
{
    RTC_TimeTypeDef time = {0};
    RTC_DateTypeDef date = {0};
    if (!BSP_RTC_ConfigureClock())
        return;
    hrtc.Instance = RTC;
    hrtc.Init.AsynchPrediv = RTC_AUTO_1_SECOND;
    hrtc.Init.OutPut = RTC_OUTPUTSOURCE_NONE;
    if (HAL_RTC_Init(&hrtc) != HAL_OK)
        return;
    BSP_RTC_SetHardwareReady(1U);
    if (BSP_RTC_HasBackup())
        return;
    date.WeekDay = RTC_WEEKDAY_MONDAY;
    date.Month = RTC_MONTH_JANUARY;
    date.Date = 1U;
    if (HAL_RTC_SetTime(&hrtc, &time, RTC_FORMAT_BCD) != HAL_OK ||
        HAL_RTC_SetDate(&hrtc, &date, RTC_FORMAT_BCD) != HAL_OK)
        BSP_RTC_SetHardwareReady(0U);
}

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
    return (uint32_t)((int64_t)days * 86400L + local->hour * 3600L + local->minute * 60L +
                      local->second - RTC_LOCAL_OFFSET);
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
    if (!BSP_RTC_IsHardwareReady())
        return 0U;
    uint32_t low = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR2);
    uint32_t high = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR3);
    uint32_t base = (high << 16U) | low;
    int64_t local_base = (int64_t)base + RTC_LOCAL_OFFSET;

    return (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR1) == RTC_BACKUP_MAGIC) &&
           (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR4) == (low ^ high ^ RTC_BACKUP_MAGIC)) &&
           (local_base >= 946684800LL) && (local_base <= 4102358400LL) &&
           (__HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY) != RESET) &&
           !__HAL_RTC_OVERFLOW_GET_FLAG(&hrtc, RTC_FLAG_OW);
}

void BSP_RTC_Init(void)
{
    memset(&data, 0, sizeof(data));
    if (!BSP_RTC_IsHardwareReady())
    {
        data.error = 1U;
        return;
    }
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
    int64_t local_time;
    uint16_t year = 1970U;
    uint8_t month = 1U;

    if (!BSP_RTC_IsHardwareReady())
    {
        BSP_RTC_SetHardwareReady(0U);
        return;
    }
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
    local_time = (int64_t)data.unix_utc + RTC_LOCAL_OFFSET;
    if ((local_time < 946684800LL) || (local_time > 4102444799LL))
    {
        data.valid = 0U;
        data.unix_utc = 0U;
        data.error = 1U;
        return;
    }
    local_seconds = (uint32_t)local_time;
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

    if (!BSP_RTC_IsHardwareReady())
    {
        BSP_RTC_SetHardwareReady(0U);
        return 0U;
    }
    if (!utc)
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
