#include "bsp_esp.h"
#include "esp_config_local.h"
#include "debug_config.h"
#include "main.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>

#define ESP_RING_SIZE       1024U
#define ESP_LINE_SIZE       192U
#define ESP_BOOT_MS         2000U
#define ESP_RECONNECT_MS    30000U
#define ESP_SNTP_WINDOW_MS  30000U
#define ESP_SNTP_RETRY_MS   60000U
#define ESP_RESYNC_MS       3600000U

static volatile uint8_t rx_ring[ESP_RING_SIZE];
static volatile uint16_t rx_head, rx_tail;
static volatile uint32_t rx_bytes, dropped_bytes, uart_errors, fault_epoch;
static uint32_t handled_fault_epoch, state_tick, command_tick, next_tick, sntp_tick;
static char line[ESP_LINE_SIZE], tx_buffer[224];
static uint16_t line_length;
static uint8_t pending, candidate_valid;
static bsp_rtc_datetime_t candidate;
static bsp_esp_data_t data;

static uint8_t parse_time(const char *text, bsp_rtc_datetime_t *value)
{
    static const char *const months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                         "Jul","Aug","Sep","Oct","Nov","Dec"};
    static const char *const weekdays[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    char weekday[4], month[4];
    unsigned int day, hour, minute, second, year;
    int used = 0;
    uint8_t known_weekday = 0U;

    if ((strlen(text) < 23U) || (strlen(text) > 24U) ||
        (sscanf(text, "%3s %3s %2u %2u:%2u:%2u %4u%n", weekday, month, &day,
                &hour, &minute, &second, &year, &used) != 7) || (text[used] != '\0') ||
        (year < 2000U) || (year > 2099U) || (day > 31U) ||
        (hour > 23U) || (minute > 59U) || (second > 59U))
        return 0U;
    for (uint8_t i = 0U; i < 7U; ++i)
        if (strcmp(weekday, weekdays[i]) == 0)
            known_weekday = 1U;
    value->month = 0U;
    for (uint8_t i = 0U; i < 12U; ++i)
        if (strcmp(month, months[i]) == 0)
            value->month = (uint8_t)(i + 1U);
    value->year = (uint16_t)year;
    value->day = (uint8_t)day;
    value->hour = (uint8_t)hour;
    value->minute = (uint8_t)minute;
    value->second = (uint8_t)second;
    return known_weekday && (BSP_RTC_ToUnixUtc(value) != 0U);
}

static void backoff(void)
{
    pending = candidate_valid = 0U;
    data.wifi_connected = 0U;
    data.state = BSP_ESP_BACKOFF;
    state_tick = HAL_GetTick();
}

static void unsupported(void)
{
    pending = candidate_valid = 0U;
    data.state = BSP_ESP_UNSUPPORTED;
    DEBUG_PRINT(DEBUG_LOG_ERRORS || DEBUG_LOG_ESP, "[ESP] AT command rejected; check 1471 GMR / PDF response\r\n");
}

static void complete_command(void)
{
    pending = 0U;
    switch (data.state)
    {
    case BSP_ESP_AT: data.state = BSP_ESP_ECHO; break;
    case BSP_ESP_ECHO: data.state = BSP_ESP_VERSION; break;
    case BSP_ESP_VERSION:
        data.state = ESP_WIFI_SSID[0] ? BSP_ESP_MODE : BSP_ESP_NO_CONFIG;
        if (data.state == BSP_ESP_NO_CONFIG)
            DEBUG_PRINT(DEBUG_LOG_ESP, "[ESP] set WiFi in BSP/esp_config_local.h, then rebuild\r\n");
        break;
    case BSP_ESP_MODE: data.state = BSP_ESP_JOIN; break;
    case BSP_ESP_JOIN:
        data.wifi_connected = 1U;
        data.state = BSP_ESP_SNTP_CONFIG;
        break;
    case BSP_ESP_SNTP_CONFIG:
        data.state = BSP_ESP_SNTP_QUERY;
        sntp_tick = HAL_GetTick();
        next_tick = HAL_GetTick() + 2000U;
        break;
    case BSP_ESP_SNTP_QUERY:
        if (candidate_valid)
        {
            data.time = candidate;
            ++data.time_sequence;
            data.state = BSP_ESP_READY;
            next_tick = HAL_GetTick() + ESP_RESYNC_MS;
        }
        else
            next_tick = HAL_GetTick() + 2000U;
        break;
    default: break;
    }
}

static void handle_line(void)
{
    if (strncmp(line, "AT+CWJAP=", 9U) == 0)
        DEBUG_PRINT(DEBUG_LOG_ESP_RX, "[ESP RX] <CWJAP echo hidden>\r\n");
    else
        DEBUG_PRINT(DEBUG_LOG_ESP_RX, "[ESP RX] %s\r\n", line);
    if (strcmp(line, "ready") == 0)
    {
        HAL_UART_AbortTransmit(&huart3);
        pending = candidate_valid = 0U;
        data.wifi_connected = 0U;
        data.state = BSP_ESP_BOOT;
        state_tick = HAL_GetTick();
        return;
    }
    if (strcmp(line, "WIFI DISCONNECT") == 0)
    {
        if (data.wifi_connected)
            backoff();
        return;
    }
    if (!pending)
        return;
    if (strcmp(line, "OK") == 0)
    {
        complete_command();
        return;
    }
    if ((strcmp(line, "ERROR") == 0) || (strcmp(line, "FAIL") == 0))
    {
        if (data.state == BSP_ESP_JOIN)
        {
            DEBUG_PRINT(DEBUG_LOG_ERRORS || DEBUG_LOG_ESP, "[ESP] WiFi join failed; retry after 30s\r\n");
            backoff();
        }
        else
            unsupported();
        return;
    }
    if (data.state == BSP_ESP_VERSION)
        DEBUG_PRINT(DEBUG_LOG_ESP, "[ESP GMR] %s\r\n", line);
    if ((data.state == BSP_ESP_SNTP_QUERY) && (strncmp(line, "+CIPSNTPTIME:", 13U) == 0))
        candidate_valid = parse_time(line + 13U, &candidate);
}

static uint8_t join_command(void)
{
    const char *fields[] = {ESP_WIFI_SSID, ESP_WIFI_PASSWORD};
    size_t used = 0U;

    if ((strlen(fields[0]) > 32U) || (strlen(fields[1]) > 64U))
        return 0U;
    memcpy(tx_buffer, "AT+CWJAP=", 9U);
    used = 9U;
    for (uint8_t i = 0U; i < 2U; ++i)
    {
        tx_buffer[used++] = '"';
        for (const char *p = fields[i]; *p; ++p)
        {
            if (((unsigned char)*p < 0x20U) || (*p == 0x7F))
                return 0U;
            if ((*p == '\\') || (*p == '"') || (*p == ','))
                tx_buffer[used++] = '\\';
            tx_buffer[used++] = *p;
        }
        tx_buffer[used++] = '"';
        if (i == 0U)
            tx_buffer[used++] = ',';
    }
    tx_buffer[used++] = '\r';
    tx_buffer[used++] = '\n';
    tx_buffer[used] = '\0';
    return 1U;
}

static void start_command(void)
{
    const char *command = NULL;
    HAL_StatusTypeDef result;

    switch (data.state)
    {
    case BSP_ESP_AT: command = "AT\r\n"; break;
    case BSP_ESP_ECHO: command = "ATE0\r\n"; break;
    case BSP_ESP_VERSION: command = "AT+GMR\r\n"; break;
    case BSP_ESP_MODE: command = "AT+CWMODE=1\r\n"; break;
    case BSP_ESP_JOIN:
        if (!join_command())
        {
            data.state = BSP_ESP_NO_CONFIG;
            DEBUG_PRINT(DEBUG_LOG_ERRORS || DEBUG_LOG_ESP, "[ESP] invalid WiFi config length/control character\r\n");
            return;
        }
        break;
    case BSP_ESP_SNTP_CONFIG: command = "AT+CIPSNTPCFG=1,8,\"ntp.aliyun.com\",\"ntp.sjtu.edu.cn\",\"cn.ntp.org.cn\"\r\n"; break;
    case BSP_ESP_SNTP_QUERY: command = "AT+CIPSNTPTIME?\r\n"; break;
    default: return;
    }
    if (command)
        strcpy(tx_buffer, command);
    result = HAL_UART_Transmit_IT(&huart3, (uint8_t *)tx_buffer, (uint16_t)strlen(tx_buffer));
    if (result == HAL_BUSY)
        return;
    if (result != HAL_OK)
    {
        backoff();
        return;
    }
    candidate_valid = 0U;
    pending = 1U;
    command_tick = HAL_GetTick();
    if (data.state == BSP_ESP_JOIN)
        DEBUG_PRINT(DEBUG_LOG_ESP, "[ESP] TX AT+CWJAP (credentials hidden)\r\n");
    else
        DEBUG_PRINT(DEBUG_LOG_ESP, "[ESP] TX %s", tx_buffer);
}

void BSP_ESP_Init(void)
{
    rx_head = rx_tail = 0U;
    rx_bytes = dropped_bytes = uart_errors = fault_epoch = handled_fault_epoch = 0U;
    line_length = pending = candidate_valid = 0U;
    memset(&data, 0, sizeof(data));
    data.state = BSP_ESP_BOOT;
    state_tick = HAL_GetTick();
    HAL_GPIO_WritePin(ESP_EN_GPIO_Port, ESP_EN_Pin, GPIO_PIN_SET);
}

void BSP_ESP_RxByte(uint8_t byte)
{
    uint16_t next = (uint16_t)((rx_head + 1U) % ESP_RING_SIZE);

    ++rx_bytes;
    if (next == rx_tail)
    {
        ++dropped_bytes;
        ++fault_epoch;
        return;
    }
    rx_ring[rx_head] = byte;
    rx_head = next;
}

void BSP_ESP_RxError(uint32_t error)
{
    (void)error;
    ++uart_errors;
    ++fault_epoch;
}

void BSP_ESP_Poll(void)
{
    uint32_t now = HAL_GetTick();

    if (handled_fault_epoch != fault_epoch)
    {
        handled_fault_epoch = fault_epoch;
        rx_tail = rx_head;
        line_length = 0U;
        /* A missing byte can turn a damaged response into a false final OK. Reset the session. */
        if ((data.state != BSP_ESP_BOOT) && (data.state != BSP_ESP_RESET_LOW) &&
            (data.state != BSP_ESP_BACKOFF))
        {
            DEBUG_PRINT(DEBUG_LOG_ERRORS || DEBUG_LOG_ESP, "[ESP] RX lost/corrupt; uart=%lu drop=%lu bad=%lu; retry after 30s\r\n",
                        (unsigned long)uart_errors, (unsigned long)dropped_bytes,
                        (unsigned long)data.bad_lines);
            backoff();
        }
    }
    for (uint16_t budget = 0U; (budget < 128U) && (rx_tail != rx_head); ++budget)
    {
        uint8_t byte = rx_ring[rx_tail];
        rx_tail = (uint16_t)((rx_tail + 1U) % ESP_RING_SIZE);
        if (byte == '\n')
        {
            line[line_length] = '\0';
            if (line_length)
                handle_line();
            line_length = 0U;
        }
        else if (byte != '\r')
        {
            if ((byte == 0U) || (line_length == ESP_LINE_SIZE - 1U))
            {
                ++data.bad_lines;
                ++fault_epoch;
                break;
            }
            line[line_length++] = (char)byte;
        }
    }
    data.rx_bytes = rx_bytes;
    data.dropped_bytes = dropped_bytes;
    data.uart_errors = uart_errors;
    now = HAL_GetTick();
    if (pending)
    {
        uint32_t timeout = data.state == BSP_ESP_JOIN ? 20000U : 2000U;
        if ((uint32_t)(now - command_tick) >= timeout)
        {
            ++data.timeouts;
            DEBUG_PRINT(DEBUG_LOG_ERRORS || DEBUG_LOG_ESP, "[ESP] response timeout state=%u; retry after 30s\r\n",
                        (unsigned int)data.state);
            backoff();
        }
        return;
    }
    if (data.state == BSP_ESP_BOOT)
    {
        if ((uint32_t)(now - state_tick) < ESP_BOOT_MS)
            return;
        /* Boot-ROM output can use a different baud rate; begin AT with a clean line. */
        rx_tail = rx_head;
        line_length = 0U;
        data.state = BSP_ESP_AT;
    }
    if (data.state == BSP_ESP_BACKOFF)
    {
        if ((uint32_t)(now - state_tick) >= ESP_RECONNECT_MS)
        {
            HAL_UART_AbortTransmit(&huart3);
            HAL_GPIO_WritePin(ESP_EN_GPIO_Port, ESP_EN_Pin, GPIO_PIN_RESET);
            data.state = BSP_ESP_RESET_LOW;
            state_tick = now;
        }
        return;
    }
    if (data.state == BSP_ESP_RESET_LOW)
    {
        if ((uint32_t)(now - state_tick) >= 100U)
        {
            HAL_GPIO_WritePin(ESP_EN_GPIO_Port, ESP_EN_Pin, GPIO_PIN_SET);
            data.state = BSP_ESP_BOOT;
            rx_tail = rx_head;
            line_length = 0U;
            state_tick = now;
        }
        return;
    }
    if (data.state == BSP_ESP_READY)
    {
        if ((int32_t)(now - next_tick) < 0)
            return;
        data.state = BSP_ESP_SNTP_QUERY;
        sntp_tick = now;
    }
    if (data.state == BSP_ESP_SNTP_QUERY)
    {
        if ((uint32_t)(now - sntp_tick) >= ESP_SNTP_WINDOW_MS)
        {
            data.state = BSP_ESP_READY;
            next_tick = now + ESP_SNTP_RETRY_MS;
            DEBUG_PRINT(DEBUG_LOG_ERRORS || DEBUG_LOG_ESP, "[ESP] SNTP has no valid time; retry after 60s, RTC retained\r\n");
            return;
        }
        if ((int32_t)(now - next_tick) < 0)
            return;
    }
    /* The IT transmitter owns tx_buffer until its TC interrupt completes. */
    if (huart3.gState == HAL_UART_STATE_READY)
        start_command();
}

const bsp_esp_data_t *BSP_ESP_GetData(void)
{
    return &data;
}
