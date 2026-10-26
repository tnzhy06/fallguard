#include "app_json.h"
#include <stdio.h>
#include <string.h>

uint8_t app_json_location(char *out, size_t size, const app_json_snapshot_t *v)
{
    int n = snprintf(out, size, "{\"id\":\"" APP_DEVICE_ID "\",\"ts\":%lu,\"lat\":%ld,\"lon\":%ld,"
                     "\"fix\":%u,\"st\":\"%s\"}", (unsigned long)v->ts,
                     (long)v->latitude, (long)v->longitude, (unsigned int)v->fix,
                     v->alarm ? "fall" : "ok");
    return (n >= 0) && ((size_t)n < size);
}

uint8_t app_json_event(char *out, size_t size, const app_json_snapshot_t *v, uint8_t fall)
{
    int n;
    if (fall)
        n = snprintf(out, size, "{\"id\":\"" APP_DEVICE_ID "\",\"ts\":%lu,\"ev\":\"fall\","
                     "\"lat\":%ld,\"lon\":%ld,\"fix\":%u}", (unsigned long)v->ts,
                     (long)v->latitude, (long)v->longitude, (unsigned int)v->fix);
    else
        n = snprintf(out, size, "{\"id\":\"" APP_DEVICE_ID "\",\"ts\":%lu,\"ev\":\"cancel\"}",
                     (unsigned long)v->ts);
    return (n >= 0) && ((size_t)n < size);
}

static const char *skip_space(const char *p)
{
    while ((*p == ' ') || (*p == '\t') || (*p == '\r') || (*p == '\n'))
        ++p;
    return p;
}

uint8_t app_json_command(const char *payload, app_json_command_t *command)
{
    const char *cmd_key = strstr(payload, "\"cmd\"");
    const char *p = skip_space(payload);
    uint8_t have_cmd = 0U, have_value = 0U;
    uint32_t seconds = 0U;
    app_json_cmd_t type = APP_CMD_LOC;

    if (!cmd_key || (*p++ != '{'))
        return 0U;
    for (;;)
    {
        uint8_t is_cmd;
        p = skip_space(p);
        if ((p == cmd_key) && !have_cmd)
        {
            is_cmd = 1U;
            p += 5U;
        }
        else if ((strncmp(p, "\"v\"", 3U) == 0) && !have_value)
        {
            is_cmd = 0U;
            p += 3U;
        }
        else
            return 0U;
        p = skip_space(p);
        if (*p++ != ':')
            return 0U;
        p = skip_space(p);
        if (is_cmd)
        {
            if (strncmp(p, "\"loc\"", 5U) == 0) {type = APP_CMD_LOC; p += 5U;}
            else if (strncmp(p, "\"clear\"", 7U) == 0) {type = APP_CMD_CLEAR; p += 7U;}
            else if (strncmp(p, "\"intv\"", 6U) == 0) {type = APP_CMD_INTERVAL; p += 6U;}
            else return 0U;
            have_cmd = 1U;
        }
        else
        {
            if ((*p < '0') || (*p > '9') || ((*p == '0') && (p[1] >= '0') && (p[1] <= '9')))
                return 0U;
            do
            {
                uint32_t digit = (uint32_t)(*p++ - '0');
                if (seconds > (UINT32_MAX - digit) / 10U)
                    return 0U;
                seconds = seconds * 10U + digit;
            } while ((*p >= '0') && (*p <= '9'));
            have_value = 1U;
        }
        p = skip_space(p);
        if (*p == '}')
            break;
        if (*p++ != ',')
            return 0U;
    }
    if (*skip_space(p + 1U) || !have_cmd ||
        ((type == APP_CMD_INTERVAL) != have_value) ||
        ((type == APP_CMD_INTERVAL) && ((seconds < APP_REPORT_MIN_SECONDS) ||
                                       (seconds > APP_REPORT_MAX_SECONDS))))
        return 0U;
    command->type = type;
    command->seconds = seconds;
    return 1U;
}
