#include "app_config.h"
#include "app_json.h"
#include "app_fall.h"
#include "app_report.h"
#include "bsp_esp.h"
#include "bsp_gps.h"
#include "bsp_mpu6050.h"
#include "main.h"
#include "rtc.h"
#include "usart.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

UART_HandleTypeDef huart3;
static RTC_TypeDef rtc_registers;
RTC_HandleTypeDef hrtc = {&rtc_registers};
uint8_t host_lse_ready = 1U, host_rtc_overflow;
static uint32_t ticks, backup[4];
static unsigned transmits;
static char transmitted[256];
static bsp_gps_data_t gps;
static bsp_mpu6050_sample_t sample;

uint32_t HAL_GetTick(void) { return ticks; }
const bsp_gps_data_t *BSP_GPS_GetData(void) { return &gps; }
const bsp_mpu6050_sample_t *BSP_MPU6050_GetSample(void) { return &sample; }
bsp_mpu6050_state_t BSP_MPU6050_GetState(void) { return BSP_MPU6050_READY; }

HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *uart, const uint8_t *bytes, uint16_t size)
{
    assert(uart == &huart3 && size < sizeof(transmitted));
    memcpy(transmitted, bytes, size);
    transmitted[size] = '\0';
    huart3.gState = HAL_UART_STATE_BUSY_TX;
    ++transmits;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *uart)
{
    assert(uart == &huart3);
    huart3.gState = HAL_UART_STATE_READY;
    return HAL_OK;
}

void HAL_GPIO_WritePin(void *port, uint16_t pins, GPIO_PinState level)
{
    assert(port == ESP_EN_GPIO_Port && pins == ESP_EN_Pin);
    (void)level;
}

uint32_t HAL_RTCEx_BKUPRead(RTC_HandleTypeDef *rtc, uint32_t reg)
{
    assert(rtc == &hrtc && reg < 4U);
    return backup[reg];
}

void HAL_RTCEx_BKUPWrite(RTC_HandleTypeDef *rtc, uint32_t reg, uint32_t value)
{
    assert(rtc == &hrtc && reg < 4U);
    backup[reg] = value;
}

HAL_StatusTypeDef HAL_RTC_SetTime(RTC_HandleTypeDef *rtc, RTC_TimeTypeDef *time, uint32_t format)
{
    assert(rtc == &hrtc && format == RTC_FORMAT_BIN);
    uint32_t counter = time->Hours * 3600U + time->Minutes * 60U + time->Seconds;
    rtc_registers.CNTH = counter >> 16U;
    rtc_registers.CNTL = counter & 0xFFFFU;
    return HAL_OK;
}

static void respond(const char *text)
{
    huart3.gState = HAL_UART_STATE_READY;
    for (; *text; ++text) BSP_ESP_RxByte((uint8_t)*text);
    BSP_ESP_Poll();
}

static void test_device_identity_and_reporting(void)
{
    char expected[256];
    app_json_snapshot_t value = {123U, 1000000, -2000000, 1U, 0U};
    assert(app_json_location(expected, sizeof(expected), &value));
    assert(strstr(expected, "\"id\":\"" APP_DEVICE_ID "\"") != NULL);
    assert(strcmp(BSP_ESP_TOPIC_LOC, APP_MQTT_TOPIC_PREFIX "/" APP_DEVICE_ID "/loc") == 0);
    assert(strcmp(BSP_ESP_TOPIC_CMD, APP_MQTT_TOPIC_PREFIX "/" APP_DEVICE_ID "/cmd") == 0);

    BSP_ESP_Init();
    ticks += ESP_BOOT_MS;
    BSP_ESP_Poll();
    assert(strcmp(transmitted, "AT\r\n") == 0);
    respond("OK\r\n");
    assert(strcmp(transmitted, "ATE0\r\n") == 0);
    respond("OK\r\n");
    assert(strcmp(transmitted, "AT+CWMODE=1\r\n") == 0);
    respond("OK\r\n");
    assert(strstr(transmitted, "AT+CWJAP=") == transmitted);
    respond("OK\r\n");
    snprintf(expected, sizeof(expected), "AT+CIPSNTPCFG=1,%d,\"%s\",\"%s\",\"%s\"\r\n",
             APP_TIMEZONE_HOURS, ESP_SNTP_SERVER_1, ESP_SNTP_SERVER_2, ESP_SNTP_SERVER_3);
    assert(strcmp(transmitted, expected) == 0);
    respond("OK\r\n");
    ticks += ESP_SNTP_QUERY_INTERVAL_MS;
    BSP_ESP_Poll();
    assert(strcmp(transmitted, "AT+CIPSNTPTIME?\r\n") == 0);
    respond("+CIPSNTPTIME:Sat Nov 14 12:34:56 2026\r\nOK\r\n");
    assert(BSP_ESP_GetData()->time_sequence == 1U);
    assert(BSP_RTC_Set(&BSP_ESP_GetData()->time));
    assert(strcmp(transmitted, "AT+MQTTCLEAN=0\r\n") == 0);
    respond("OK\r\n");
    assert(strstr(transmitted, "AT+MQTTUSERCFG=0,1,\"" APP_DEVICE_ID "\",") == transmitted);
    respond("OK\r\n");
    snprintf(expected, sizeof(expected), "AT+MQTTCONNCFG=0,%u,0,\"\",\"\",0,0\r\n",
             (unsigned int)MQTT_KEEPALIVE_SECONDS);
    assert(strcmp(transmitted, expected) == 0);
    respond("OK\r\n");
    snprintf(expected, sizeof(expected), "+MQTTCONNECTED:0,1,\"%s\",%u,\"\",0\r\nOK\r\n",
             MQTT_HOST, (unsigned int)MQTT_PORT);
    respond(expected);
    assert(strstr(transmitted, "AT+MQTTSUB=0,\"" BSP_ESP_TOPIC_CMD "\",0") == transmitted);
    respond("OK\r\n");
    assert(BSP_ESP_IsOnline());

    app_fall_init();
    app_report_init();
    assert(app_report_get_data()->interval_seconds == APP_REPORT_DEFAULT_SECONDS);
    app_report_poll();
    assert(strstr(transmitted, "AT+MQTTPUB=0,\"" BSP_ESP_TOPIC_LOC "\",") == transmitted);
    assert(strstr(transmitted, "\\\"id\\\":\\\"" APP_DEVICE_ID "\\\"") != NULL);
    respond("OK\r\n");
    unsigned before = transmits;
    ticks += APP_REPORT_DEFAULT_SECONDS * 1000U - 1U;
    app_report_poll();
    assert(transmits == before);
    ++ticks;
    app_report_poll();
    assert(transmits == before + 1U);
}

static void test_configured_fall_threshold(void)
{
    app_fall_init();
    sample.angle_cdeg[0] = (int32_t)APP_FALL_ANGLE_DEG * 100 - 1;
    sample.tick = ticks;
    ++sample.samples;
    app_fall_poll();
    assert(app_fall_get_data()->state == APP_FALL_IDLE);
    sample.angle_cdeg[0] = -(int32_t)APP_FALL_ANGLE_DEG * 100;
    ++ticks;
    sample.tick = ticks;
    ++sample.samples;
    app_fall_poll();
    assert(app_fall_get_data()->state == APP_FALL_HOLDING);
    for (uint32_t elapsed = 1U; elapsed <= APP_FALL_HOLD_MS; ++elapsed)
    {
        sample.tick = ++ticks;
        ++sample.samples;
        app_fall_poll();
        assert(app_fall_get_data()->state == (elapsed == APP_FALL_HOLD_MS ? APP_FALL_ALARM : APP_FALL_HOLDING));
    }
}

static void test_timezone_and_backup_boundaries(void)
{
    const bsp_rtc_datetime_t first = {2000U, 1U, 1U, 0U, 0U, 0U};
    const bsp_rtc_datetime_t last = {2099U, 12U, 31U, 23U, 59U, 59U};
    assert(BSP_RTC_ToUnixUtc(&first) == (uint32_t)(946684800LL - APP_TIMEZONE_HOURS * 3600LL));
    assert(BSP_RTC_Set(&first) && BSP_RTC_HasBackup());
    BSP_RTC_Init();
    assert(BSP_RTC_GetData()->restored && BSP_RTC_GetData()->local.year == 2000U);
    assert(BSP_RTC_Set(&last) && BSP_RTC_HasBackup());
    assert(BSP_RTC_GetData()->unix_utc == (uint32_t)(4102444799LL - APP_TIMEZONE_HOURS * 3600LL));
    BSP_RTC_Init();
    assert(BSP_RTC_GetData()->restored && BSP_RTC_GetData()->local.year == 2099U);
    rtc_registers.CNTH = 86400U >> 16U;
    rtc_registers.CNTL = 86400U & 0xFFFFU;
    BSP_RTC_Poll();
    assert(!BSP_RTC_GetData()->valid);
    if (APP_TIMEZONE_HOURS > 0)
    {
        assert(BSP_RTC_Set(&first));
        uint32_t base = backup[RTC_BKP_DR2] | (backup[RTC_BKP_DR3] << 16U);
        uint32_t elapsed = UINT32_MAX - base;
        rtc_registers.CNTH = elapsed >> 16U;
        rtc_registers.CNTL = elapsed & 0xFFFFU;
        BSP_RTC_Poll();
        assert(!BSP_RTC_GetData()->valid); /* Local offset must not wrap into 1970. */
    }
}

int main(void)
{
    test_device_identity_and_reporting();
    test_configured_fall_threshold();
    test_timezone_and_backup_boundaries();
    puts("Unified configuration tests passed");
    return 0;
}
