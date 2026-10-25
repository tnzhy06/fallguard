#ifndef BSP_ESP_H
#define BSP_ESP_H

#include "bsp_rtc.h"

typedef enum
{
    BSP_ESP_BOOT,
    BSP_ESP_AT,
    BSP_ESP_ECHO,
    BSP_ESP_VERSION,
    BSP_ESP_MODE,
    BSP_ESP_JOIN,
    BSP_ESP_SNTP_CONFIG,
    BSP_ESP_SNTP_QUERY,
    BSP_ESP_READY,
    BSP_ESP_BACKOFF,
    BSP_ESP_RESET_LOW,
    BSP_ESP_NO_CONFIG,
    BSP_ESP_UNSUPPORTED
} bsp_esp_state_t;

typedef struct
{
    bsp_esp_state_t state;
    bsp_rtc_datetime_t time;
    uint32_t time_sequence, rx_bytes, dropped_bytes, uart_errors, bad_lines, timeouts;
    uint8_t wifi_connected;
} bsp_esp_data_t;

void BSP_ESP_Init(void);
void BSP_ESP_Poll(void);
void BSP_ESP_RxByte(uint8_t byte);
void BSP_ESP_RxError(uint32_t error);
const bsp_esp_data_t *BSP_ESP_GetData(void);

#endif
