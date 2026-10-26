#ifndef BSP_ESP_H
#define BSP_ESP_H

#include "bsp_rtc.h"
#include <stddef.h>

#define BSP_ESP_TOPIC_LOC   "fallguard/belt01/loc"
#define BSP_ESP_TOPIC_ALARM "fallguard/belt01/alarm"
#define BSP_ESP_TOPIC_CMD   "fallguard/belt01/cmd"
#define BSP_ESP_PAYLOAD_SIZE 128U

typedef enum
{
    BSP_ESP_BOOT,
    BSP_ESP_AT,
    BSP_ESP_ECHO,
    BSP_ESP_MODE,
    BSP_ESP_JOIN,
    BSP_ESP_SNTP_CONFIG,
    BSP_ESP_SNTP_QUERY,
    BSP_ESP_READY,
    BSP_ESP_BACKOFF,
    BSP_ESP_RESET_LOW,
    BSP_ESP_NO_CONFIG,
    BSP_ESP_UNSUPPORTED,
    BSP_ESP_MQTT_CLEAN,
    BSP_ESP_MQTT_USER,
    BSP_ESP_MQTT_CONFIG,
    BSP_ESP_MQTT_CONNECT,
    BSP_ESP_MQTT_SUBSCRIBE,
    BSP_ESP_MQTT_ONLINE,
    BSP_ESP_MQTT_PUBLISH
} bsp_esp_state_t;

typedef struct
{
    bsp_esp_state_t state;
    bsp_rtc_datetime_t time;
    uint32_t time_sequence, rx_bytes, dropped_bytes, uart_errors, bad_lines, timeouts;
    uint32_t published, publish_failures, command_drops;
    uint8_t wifi_connected, mqtt_connected, mqtt_subscribed;
} bsp_esp_data_t;

void BSP_ESP_Init(void);
void BSP_ESP_Poll(void);
void BSP_ESP_RxByte(uint8_t byte);
void BSP_ESP_RxError(uint32_t error);
uint8_t BSP_ESP_IsOnline(void);
uint8_t BSP_ESP_Publish(const char *topic, const char *payload);
uint8_t BSP_ESP_ReadCommand(char *payload, size_t size);
const bsp_esp_data_t *BSP_ESP_GetData(void);

#endif
