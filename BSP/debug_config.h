#ifndef DEBUG_CONFIG_H
#define DEBUG_CONFIG_H

#include <stdio.h>

#define DEBUG_OUTPUT_ENABLE       1

#define DEBUG_LOG_BOOT            0
#define DEBUG_LOG_STATUS          0
#define DEBUG_LOG_KEYS            0
#define DEBUG_LOG_ERRORS          0
#define DEBUG_LOG_FALL            0
#define DEBUG_LOG_JQ              0
#define DEBUG_LOG_ESP             1
#define DEBUG_LOG_ESP_RX          1
#define DEBUG_LOG_MQTT            1
#define DEBUG_LOG_REPORT          1
#define DEBUG_LOG_TIME            1
#define DEBUG_LOG_TIME_PERIODIC   0
#define DEBUG_TIME_PERIOD_MS      1000U

#define DEBUG_LOG_MPU_ACCEL       0
#define DEBUG_LOG_MPU_GYRO        0
#define DEBUG_LOG_MPU_ANGLES      0
#define DEBUG_MPU_PERIOD_MS       200U

#define DEBUG_LOG_GPS_FIX         0
#define DEBUG_LOG_GPS_POSITION    0
#define DEBUG_LOG_GPS_UTC         0
#define DEBUG_LOG_GPS_ALTITUDE    0
#define DEBUG_LOG_GPS_STATS       0
#define DEBUG_LOG_GPS_SIGNAL      0
#define DEBUG_GPS_PERIOD_MS       250U

/* Keep filtering at the call site so disabled messages do not evaluate their arguments. */
#define DEBUG_PRINT(enabled, ...) \
    do { if (DEBUG_OUTPUT_ENABLE && (enabled)) { printf(__VA_ARGS__); } } while (0)

#endif
