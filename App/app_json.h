#ifndef APP_JSON_H
#define APP_JSON_H
#include <stddef.h>
#include <stdint.h>

#define APP_DEVICE_ID "belt01"
#define APP_REPORT_MIN_SECONDS 1U
#define APP_REPORT_MAX_SECONDS 3600U

typedef struct
{
    uint32_t ts;
    int32_t latitude, longitude;
    uint8_t fix, alarm;
} app_json_snapshot_t;

typedef enum {APP_CMD_LOC, APP_CMD_CLEAR, APP_CMD_INTERVAL} app_json_cmd_t;
typedef struct {app_json_cmd_t type; uint32_t seconds;} app_json_command_t;

uint8_t app_json_location(char *out, size_t size, const app_json_snapshot_t *value);
uint8_t app_json_event(char *out, size_t size, const app_json_snapshot_t *value, uint8_t fall);
uint8_t app_json_command(const char *payload, app_json_command_t *command);
#endif
