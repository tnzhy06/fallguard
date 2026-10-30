#ifndef DEBUG_CONFIG_H
#define DEBUG_CONFIG_H

#include <stdio.h>

#define DEBUG_OUTPUT_ENABLE       0

#define DEBUG_LOG_BOOT            1
#define DEBUG_LOG_STATUS          1
#define DEBUG_LOG_KEYS            1
#define DEBUG_LOG_ERRORS          1
#define DEBUG_LOG_FALL            1
#define DEBUG_LOG_AUDIO           1
#define DEBUG_LOG_ESP             1
#define DEBUG_LOG_ESP_RX          1
#define DEBUG_LOG_MQTT            1
#define DEBUG_LOG_REPORT          1
#define DEBUG_LOG_TIME            1
#define DEBUG_LOG_TIME_PERIODIC   1
#define DEBUG_TIME_PERIOD_MS      1000U

#define DEBUG_LOG_MPU_ACCEL       1
#define DEBUG_LOG_MPU_GYRO        1
#define DEBUG_LOG_MPU_ANGLES      1
#define DEBUG_MPU_PERIOD_MS       200U

#define DEBUG_LOG_GPS_FIX         1
#define DEBUG_LOG_GPS_POSITION    1
#define DEBUG_LOG_GPS_UTC         1
#define DEBUG_LOG_GPS_ALTITUDE    1
#define DEBUG_LOG_GPS_STATS       1
#define DEBUG_LOG_GPS_SIGNAL      1
#define DEBUG_GPS_PERIOD_MS       250U

/* Keep filtering at the call site so disabled messages do not evaluate their arguments. */
#define DEBUG_PRINT(enabled, ...) \
    do { if (DEBUG_OUTPUT_ENABLE && (enabled)) { printf(__VA_ARGS__); } } while (0)

#endif
