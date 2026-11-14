#ifndef BSP_LOG_H
#define BSP_LOG_H

#include "app_config.h"
#include <stdint.h>
#include <stdio.h>

/* Disabled messages do not evaluate their arguments. */
#define DEBUG_PRINT(enabled, ...)                                                \
    do {                                                                        \
        if (DEBUG_OUTPUT_ENABLE && (enabled)) {                                  \
            printf(__VA_ARGS__);                                                \
        }                                                                       \
    } while (0)

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
