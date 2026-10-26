#include "bsp_esp.h"
#include "esp_config_local.h"
#include "mqtt_config_local.h"
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
static char line[ESP_LINE_SIZE], tx_buffer[256];
static uint16_t line_length;
static uint8_t pending, candidate_valid, time_disabled, connect_ok;
static uint16_t payload_left, payload_used;
static uint32_t payload_tick;
static uint8_t payload_accept;
static char payload_buffer[BSP_ESP_PAYLOAD_SIZE];
static char commands[4][BSP_ESP_PAYLOAD_SIZE];
static uint8_t command_head, command_tail, command_count;
static bsp_rtc_datetime_t candidate;
static bsp_esp_data_t data;

uint8_t BSP_ESP_IsOnline(void)
{
    return data.wifi_connected && data.mqtt_connected && data.mqtt_subscribed;
}

static uint8_t append_quoted(char *out, size_t size, size_t *used, const char *text)
{
    if (*used + 2U >= size)
        return 0U;
    out[(*used)++] = '"';
    for (; *text; ++text)
    {
        if (((unsigned char)*text < 0x20U) || (*text == 0x7F) || (*used + 3U >= size))
            return 0U;
        if ((*text == '\\') || (*text == '"') || (*text == ','))
            out[(*used)++] = '\\';
        out[(*used)++] = *text;
    }
    out[(*used)++] = '"';
    out[*used] = '\0';
    return 1U;
}

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
    if (data.state == BSP_ESP_MQTT_PUBLISH)
        ++data.publish_failures;
    pending = candidate_valid = 0U;
    data.wifi_connected = 0U;
    data.mqtt_connected = data.mqtt_subscribed = connect_ok = 0U;
    command_head = command_tail = command_count = 0U;
    payload_left = payload_used = 0U;
    data.state = BSP_ESP_BACKOFF;
    state_tick = HAL_GetTick();
}

static void unsupported(void)
{
    pending = candidate_valid = 0U;
    data.state = BSP_ESP_UNSUPPORTED;
    DEBUG_PRINT(DEBUG_LOG_ERRORS || DEBUG_LOG_ESP, "[ESP] AT command rejected; check ESP response / PDF\r\n");
}

static void complete_command(void)
{
    pending = 0U;
    switch (data.state)
    {
    case BSP_ESP_AT: data.state = BSP_ESP_ECHO; break;
    case BSP_ESP_ECHO:
        data.state = ESP_WIFI_SSID[0] ? BSP_ESP_MODE : BSP_ESP_NO_CONFIG;
        if (data.state == BSP_ESP_NO_CONFIG)
            DEBUG_PRINT(DEBUG_LOG_ESP, "[ESP] set WiFi in BSP/esp_config_local.h, then rebuild\r\n");
        break;
    case BSP_ESP_MODE: data.state = BSP_ESP_JOIN; break;
    case BSP_ESP_JOIN:
        data.wifi_connected = 1U;
        data.state = time_disabled ? BSP_ESP_READY : BSP_ESP_SNTP_CONFIG;
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
    case BSP_ESP_MQTT_CLEAN: data.state = BSP_ESP_MQTT_USER; break;
    case BSP_ESP_MQTT_USER: data.state = BSP_ESP_MQTT_CONFIG; break;
    case BSP_ESP_MQTT_CONFIG: data.state = BSP_ESP_MQTT_CONNECT; connect_ok = 0U; break;
    case BSP_ESP_MQTT_CONNECT:
        connect_ok = 1U;
        if (data.mqtt_connected)
            data.state = BSP_ESP_MQTT_SUBSCRIBE;
        else
            pending = 1U;
        break;
    case BSP_ESP_MQTT_SUBSCRIBE:
        data.mqtt_subscribed = 1U;
        data.state = BSP_ESP_MQTT_ONLINE;
        DEBUG_PRINT(DEBUG_LOG_MQTT, "[MQTT] connected and command topic subscribed\r\n");
        break;
    case BSP_ESP_MQTT_PUBLISH:
        ++data.published;
        data.state = BSP_ESP_MQTT_ONLINE;
        DEBUG_PRINT(DEBUG_LOG_MQTT, "[MQTT] publish acknowledged by AT\r\n");
        break;
    default: break;
    }
}

static void handle_line(void)
{
    if ((strncmp(line, "AT+CWJAP=", 9U) == 0) || (strncmp(line, "AT+MQTTUSERCFG=", 15U) == 0))
        DEBUG_PRINT(DEBUG_LOG_ESP_RX, "[ESP RX] <credential command echo hidden>\r\n");
    else
        DEBUG_PRINT(DEBUG_LOG_ESP_RX, "[ESP RX] %s\r\n", line);
    if (strcmp(line, "ready") == 0)
    {
        HAL_UART_AbortTransmit(&huart3);
        pending = candidate_valid = 0U;
        data.wifi_connected = 0U;
        data.mqtt_connected = data.mqtt_subscribed = 0U;
        command_head = command_tail = command_count = 0U;
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
    if (strcmp(line, "+MQTTDISCONNECTED:0") == 0)
    {
        if (data.mqtt_connected || data.mqtt_subscribed || (data.state == BSP_ESP_MQTT_CONNECT))
        {
            DEBUG_PRINT(DEBUG_LOG_MQTT, "[MQTT] disconnected; reconnect after 30s\r\n");
            backoff();
        }
        return;
    }
    if (strncmp(line, "+MQTTCONNECTED:", 15U) == 0)
    {
        char expected[ESP_LINE_SIZE];
        int n = snprintf(expected, sizeof(expected), "+MQTTCONNECTED:0,1,\"%s\",\"%u\",\"\",0",
                         MQTT_HOST, (unsigned int)MQTT_PORT);
        uint8_t matched = (n > 0) && ((size_t)n < sizeof(expected)) &&
                          (strcmp(line, expected) == 0);
        /* The installed firmware quotes the port; the PDF example leaves it numeric. */
        if (!matched)
        {
            n = snprintf(expected, sizeof(expected), "+MQTTCONNECTED:0,1,\"%s\",%u,\"\",0",
                         MQTT_HOST, (unsigned int)MQTT_PORT);
            matched = (n > 0) && ((size_t)n < sizeof(expected)) && (strcmp(line, expected) == 0);
        }
        if ((data.state == BSP_ESP_MQTT_CONNECT) && matched)
        {
            data.mqtt_connected = 1U;
            if (connect_ok)
                complete_command();
        }
        return;
    }
    if (!pending)
        return;
    if ((strcmp(line, "OK") == 0) ||
        ((data.state == BSP_ESP_MQTT_SUBSCRIBE) && (strcmp(line, "ALREADY SUBSCRIBE") == 0)))
    {
        complete_command();
        return;
    }
    if ((strcmp(line, "ERROR") == 0) || (strcmp(line, "FAIL") == 0))
    {
        if ((data.state == BSP_ESP_SNTP_CONFIG) || (data.state == BSP_ESP_SNTP_QUERY))
        {
            pending = 0U;
            time_disabled = 1U;
            data.state = BSP_ESP_READY;
            DEBUG_PRINT(DEBUG_LOG_ESP, "[ESP] SNTP rejected; RTC retained, MQTT may continue\r\n");
        }
        else if (data.state == BSP_ESP_MQTT_CLEAN)
            complete_command();
        else if (data.state >= BSP_ESP_MQTT_USER)
        {
            DEBUG_PRINT(DEBUG_LOG_MQTT, "[MQTT] command failed state=%u\r\n", (unsigned int)data.state);
            backoff();
        }
        else if (data.state == BSP_ESP_JOIN)
        {
            DEBUG_PRINT(DEBUG_LOG_ERRORS || DEBUG_LOG_ESP, "[ESP] WiFi join failed; retry after 30s\r\n");
            backoff();
        }
        else
            unsupported();
        return;
    }
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
    size_t used;
    int n;

    switch (data.state)
    {
    case BSP_ESP_AT: command = "AT\r\n"; break;
    case BSP_ESP_ECHO: command = "ATE0\r\n"; break;
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
    case BSP_ESP_MQTT_CLEAN: command = "AT+MQTTCLEAN=0\r\n"; break;
    case BSP_ESP_MQTT_USER:
        n = snprintf(tx_buffer, sizeof(tx_buffer), "AT+MQTTUSERCFG=0,1,\"belt01\",");
        used = n > 0 ? (size_t)n : sizeof(tx_buffer);
        if ((strlen(MQTT_USERNAME) > 64U) || (strlen(MQTT_PASSWORD) > 64U) ||
            !append_quoted(tx_buffer, sizeof(tx_buffer), &used, MQTT_USERNAME) ||
            (used + 1U >= sizeof(tx_buffer)))
            goto bad_config;
        tx_buffer[used++] = ',';
        if (!append_quoted(tx_buffer, sizeof(tx_buffer), &used, MQTT_PASSWORD) ||
            (used + sizeof(",0,0,\"\"\r\n") > sizeof(tx_buffer)))
            goto bad_config;
        strcpy(tx_buffer + used, ",0,0,\"\"\r\n");
        break;
    case BSP_ESP_MQTT_CONFIG: command = "AT+MQTTCONNCFG=0,120,0,\"\",\"\",0,0\r\n"; break;
    case BSP_ESP_MQTT_CONNECT:
        used = strlen("AT+MQTTCONN=0,");
        strcpy(tx_buffer, "AT+MQTTCONN=0,");
        if ((strlen(MQTT_HOST) > 128U) || !MQTT_PORT || (MQTT_PORT > 65535U) ||
            !append_quoted(tx_buffer, sizeof(tx_buffer), &used, MQTT_HOST))
            goto bad_config;
        n = snprintf(tx_buffer + used, sizeof(tx_buffer) - used, ",%u,0\r\n", (unsigned int)MQTT_PORT);
        if ((n < 0) || ((size_t)n >= sizeof(tx_buffer) - used))
            goto bad_config;
        break;
    case BSP_ESP_MQTT_SUBSCRIBE:
        command = "AT+MQTTSUB=0,\"" BSP_ESP_TOPIC_CMD "\",0\r\n";
        break;
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
    else if (data.state == BSP_ESP_MQTT_USER)
        DEBUG_PRINT(DEBUG_LOG_ESP, "[ESP] TX AT+MQTTUSERCFG (credentials hidden)\r\n");
    else
        DEBUG_PRINT(DEBUG_LOG_ESP, "[ESP] TX %s", tx_buffer);
    return;
bad_config:
    data.state = BSP_ESP_NO_CONFIG;
    DEBUG_PRINT(DEBUG_LOG_MQTT, "[MQTT] invalid config or AT command exceeds 255 bytes\r\n");
}

void BSP_ESP_Init(void)
{
    rx_head = rx_tail = 0U;
    rx_bytes = dropped_bytes = uart_errors = fault_epoch = handled_fault_epoch = 0U;
    line_length = pending = candidate_valid = 0U;
    time_disabled = connect_ok = 0U;
    payload_left = payload_used = 0U;
    command_head = command_tail = command_count = 0U;
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

static uint8_t header_number(const char **p, uint32_t *value)
{
    *value = 0U;
    if ((**p < '0') || (**p > '9'))
        return 0U;
    do
    {
        uint32_t digit = (uint32_t)(*(*p)++ - '0');
        if (*value > (4096U - digit) / 10U)
            return 0U;
        *value = *value * 10U + digit;
    } while ((**p >= '0') && (**p <= '9'));
    return 1U;
}

static void finish_payload(void)
{
    if (payload_accept && data.wifi_connected && data.mqtt_connected && (command_count < 4U))
    {
        payload_buffer[payload_used] = '\0';
        memcpy(commands[command_head], payload_buffer, payload_used + 1U);
        command_head = (uint8_t)((command_head + 1U) % 4U);
        ++command_count;
    }
    else
        ++data.command_drops;
}

static uint8_t sub_header(void)
{
    const char *p = line + 13U, *topic, *end;
    uint32_t link, length;

    if (!header_number(&p, &link) || (*p++ != ',') || (*p++ != '"'))
        return 0U;
    topic = p;
    end = strchr(topic, '"');
    if (!end || (end[1] != ','))
        return 0U;
    p = end + 2U;
    if (!header_number(&p, &length) || (*p++ != ',') || *p)
        return 0U;
    payload_left = (uint16_t)length;
    payload_used = 0U;
    payload_tick = HAL_GetTick();
    payload_accept = (link == 0U) && (length < sizeof(payload_buffer)) &&
                     ((size_t)(end - topic) == strlen(BSP_ESP_TOPIC_CMD)) &&
                     (memcmp(topic, BSP_ESP_TOPIC_CMD, (size_t)(end - topic)) == 0);
    if (!length)
        finish_payload();
    return 1U;
}

uint8_t BSP_ESP_ReadCommand(char *payload, size_t size)
{
    size_t length;
    if (!BSP_ESP_IsOnline() || !command_count)
        return 0U;
    length = strlen(commands[command_tail]);
    if (length + 1U > size)
        return 0U;
    memcpy(payload, commands[command_tail], length + 1U);
    command_tail = (uint8_t)((command_tail + 1U) % 4U);
    --command_count;
    return 1U;
}

uint8_t BSP_ESP_Publish(const char *topic, const char *payload)
{
    size_t used;
    HAL_StatusTypeDef result;
    if (!BSP_ESP_IsOnline() || (data.state != BSP_ESP_MQTT_ONLINE) || pending ||
        (huart3.gState != HAL_UART_STATE_READY))
        return 0U;
    strcpy(tx_buffer, "AT+MQTTPUB=0,");
    used = strlen(tx_buffer);
    if ((strlen(topic) > 128U) || !append_quoted(tx_buffer, sizeof(tx_buffer), &used, topic) ||
        (used + 1U >= sizeof(tx_buffer)))
        return 0U;
    tx_buffer[used++] = ',';
    if (!append_quoted(tx_buffer, sizeof(tx_buffer), &used, payload) ||
        (used + sizeof(",0,0\r\n") > sizeof(tx_buffer)))
        return 0U;
    strcpy(tx_buffer + used, ",0,0\r\n");
    result = HAL_UART_Transmit_IT(&huart3, (uint8_t *)tx_buffer, (uint16_t)strlen(tx_buffer));
    if (result != HAL_OK)
    {
        if (result != HAL_BUSY)
        {
            ++data.publish_failures;
            backoff();
        }
        return 0U;
    }
    data.state = BSP_ESP_MQTT_PUBLISH;
    pending = 1U;
    command_tick = HAL_GetTick();
    DEBUG_PRINT(DEBUG_LOG_MQTT, "[MQTT] publish queued topic=%s\r\n", topic);
    return 1U;
}

void BSP_ESP_Poll(void)
{
    uint32_t now = HAL_GetTick();

    if (payload_left && ((uint32_t)(now - payload_tick) >= 2000U))
    {
        ++data.bad_lines;
        ++fault_epoch;
    }

    if (handled_fault_epoch != fault_epoch)
    {
        handled_fault_epoch = fault_epoch;
        rx_tail = rx_head;
        line_length = 0U;
        payload_left = payload_used = 0U;
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
        if (payload_left)
        {
            if (payload_accept)
            {
                if (!byte)
                    payload_accept = 0U;
                else
                    payload_buffer[payload_used++] = (char)byte;
            }
            if (--payload_left == 0U)
                finish_payload();
            continue;
        }
        if (byte == '\n')
        {
            line[line_length] = '\0';
            if (strncmp(line, "+MQTTSUBRECV:", 13U) == 0)
            {
                ++data.bad_lines;
                ++fault_epoch;
                backoff();
                break;
            }
            else if (line_length)
                handle_line();
            line_length = 0U;
        }
        else if (byte != '\r')
        {
            if ((byte == 0U) || (line_length == ESP_LINE_SIZE - 1U))
            {
                ++data.bad_lines;
                ++fault_epoch;
                if (data.state != BSP_ESP_BOOT)
                    backoff();
                break;
            }
            line[line_length++] = (char)byte;
            line[line_length] = '\0';
            if ((byte == ',') && (strncmp(line, "+MQTTSUBRECV:", 13U) == 0) && sub_header())
                line_length = 0U;
        }
    }
    data.rx_bytes = rx_bytes;
    data.dropped_bytes = dropped_bytes;
    data.uart_errors = uart_errors;
    now = HAL_GetTick();
    if (pending)
    {
        uint32_t timeout = data.state == BSP_ESP_JOIN ? 20000U :
                           (data.state == BSP_ESP_MQTT_CONNECT ? 30000U :
                           (data.state >= BSP_ESP_MQTT_CLEAN ? 10000U : 2000U));
        if ((uint32_t)(now - command_tick) >= timeout)
        {
            ++data.timeouts;
            if ((data.state == BSP_ESP_SNTP_CONFIG) || (data.state == BSP_ESP_SNTP_QUERY))
                time_disabled = 1U;
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
    if ((data.state == BSP_ESP_READY) || (data.state == BSP_ESP_MQTT_ONLINE))
    {
        if (!BSP_ESP_IsOnline() && MQTT_HOST[0])
            data.state = BSP_ESP_MQTT_CLEAN;
        else if (!time_disabled && ((int32_t)(now - next_tick) >= 0))
        {
            data.state = BSP_ESP_SNTP_QUERY;
            sntp_tick = now;
        }
        else
        {
            if (BSP_ESP_IsOnline())
                data.state = BSP_ESP_MQTT_ONLINE;
            return;
        }
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
