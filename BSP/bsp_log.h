#ifndef BSP_LOG_H
#define BSP_LOG_H

#include "app_config.h"
#include <stdint.h>
#include <stdio.h>

/* Stable module tags keep every serial prefix consistent. */
#define LOG_TAG_AUDIO "AUDIO"
#define LOG_TAG_BOOT "BOOT"
#define LOG_TAG_ESP "ESP"
#define LOG_TAG_FALL "FALL"
#define LOG_TAG_GPS "GPS"
#define LOG_TAG_KEY "KEY"
#define LOG_TAG_LCD "LCD"
#define LOG_TAG_LOG "LOG"
#define LOG_TAG_MPU "MPU"
#define LOG_TAG_MQTT "MQTT"
#define LOG_TAG_REPORT "REPORT"
#define LOG_TAG_RTC "RTC"
#define LOG_TAG_TIM "TIM"

/* Disabled messages do not evaluate their arguments. The first variadic
 * argument must be a string literal so the prefix is joined at compile time. */
#define LOG_WRITE(enabled, level, module, ...)                                  \
    do {                                                                        \
        if (DEBUG_OUTPUT_ENABLE && (enabled)) {                                  \
            printf("[" level "][" module "] " __VA_ARGS__);                  \
        }                                                                       \
    } while (0)

#define LOG_INFO(enabled, module, ...)                                           \
    LOG_WRITE(DEBUG_LOG_INFO_ENABLE && (enabled), "I", module, __VA_ARGS__)
#define LOG_WARN(enabled, module, ...)                                           \
    LOG_WRITE(DEBUG_LOG_WARN_ENABLE && (enabled), "W", module, __VA_ARGS__)
#define LOG_ERROR(enabled, module, ...)                                          \
    LOG_WRITE(DEBUG_LOG_ERROR_ENABLE && (enabled), "E", module, __VA_ARGS__)
#define LOG_DEBUG(enabled, module, ...)                                          \
    LOG_WRITE(DEBUG_LOG_DEBUG_ENABLE && (enabled), "D", module, __VA_ARGS__)

typedef struct
{
    volatile uint32_t sent_bytes;
    volatile uint32_t dropped_bytes;
    volatile uint32_t errors;
} bsp_log_data_t;

/* Call once after USART1 initialization, before the first log write. */
void BSP_LOG_Init(void);
/* Best-effort enqueue. Excess bytes are counted and dropped without waiting. */
void BSP_LOG_Write(const char *text, uint16_t length);
void BSP_LOG_Poll(void);
void BSP_LOG_TxComplete(void);
void BSP_LOG_TxError(void);
const bsp_log_data_t *BSP_LOG_GetData(void);

#endif
