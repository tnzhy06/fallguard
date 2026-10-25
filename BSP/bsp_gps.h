#ifndef BSP_GPS_H
#define BSP_GPS_H

#include <stdint.h>

typedef enum
{
    BSP_GPS_SIGNAL_NONE = 0,
    BSP_GPS_SIGNAL_WEAK,
    BSP_GPS_SIGNAL_MEDIUM,
    BSP_GPS_SIGNAL_STRONG
} bsp_gps_signal_level_t;

typedef struct
{
    int32_t latitude_udeg, longitude_udeg;
    int32_t altitude_cm;
    uint32_t position_tick, age_ms;
    uint32_t rx_bytes, gga_count, rmc_count;
    uint32_t checksum_errors, bad_frames, dropped_bytes;
    uint32_t uart_errors, uart_error_mask;
    uint32_t gsv_count, signal_tick, signal_age_ms;
    uint8_t satellites_in_view, signal_count, signal_max_dbhz;
    uint8_t gsv_valid, signal_valid;
    bsp_gps_signal_level_t signal_level;
    uint32_t hdop_x100;
    uint8_t fix, has_position, satellites, hdop_valid;
    uint8_t utc_hour, utc_minute, utc_second;
    uint8_t utc_day, utc_month, utc_year;
    uint8_t utc_valid;
} bsp_gps_data_t;

void BSP_GPS_Init(void);
void BSP_GPS_RxByte(uint8_t byte);
void BSP_GPS_RxError(uint32_t error);
void BSP_GPS_Poll(void);
const bsp_gps_data_t *BSP_GPS_GetData(void);

#endif
