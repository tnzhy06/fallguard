#include "bsp_gps.h"
#include "stm32f1xx_hal.h"
#include "lwgps/lwgps.h"
#include <string.h>

#define GPS_RING_SIZE 1024U
#define GPS_FRAME_SIZE 128U
#define GPS_STALE_MS 5000U

static volatile uint8_t rx_ring[GPS_RING_SIZE];
static volatile uint16_t rx_head, rx_tail;
static volatile uint32_t rx_bytes, dropped_bytes, uart_errors, uart_error_mask, fault_epoch;
static uint32_t handled_fault_epoch;
static char frame[GPS_FRAME_SIZE];
static uint16_t frame_length;
static lwgps_t parser;
static lwgps_statement_t parsed_statement;
static bsp_gps_data_t data;
static uint8_t gga_valid, rmc_valid, have_hdop;
static uint32_t gga_tick, rmc_tick, hdop_tick;
static uint8_t have_gsv, gsv_active, gsv_good, gsv_next, gsv_total, gsv_visible;
static uint8_t gsv_view_count, gsv_signal_count;
static char gsv_talker[2];
static uint64_t gsv_talkers_seen;
static uint32_t gsv_start_tick, gsv_last_tick;
static uint32_t candidate_sequence, committed_sequence;
static int32_t candidate_latitude, candidate_longitude, candidate_altitude;

static int32_t rounded(double value)
{
    return (int32_t)(value + ((value >= 0.0) ? 0.5 : -0.5));
}

static uint8_t digits(const char *text, unsigned int count)
{
    for (unsigned int i = 0U; i < count; ++i)
        if ((text[i] < '0') || (text[i] > '9'))
            return 0U;
    return 1U;
}

static uint8_t decimal_valid(const char *text, uint8_t signed_value)
{
    uint8_t point = 0U, count = 0U;
    if ((*text == '-') && (signed_value != 0U))
        ++text;
    for (; *text != '\0'; ++text)
    {
        if ((*text >= '0') && (*text <= '9'))
            ++count;
        else if ((*text == '.') && (point == 0U))
            point = 1U;
        else
            return 0U;
    }
    return (count != 0U);
}

static uint8_t coordinate_valid(const char *text, const char *hemisphere, uint8_t latitude)
{
    unsigned int whole = latitude ? 4U : 5U;
    unsigned int degrees = 0U, minutes;

    if ((strlen(text) < whole) || (digits(text, whole) == 0U) ||
        ((text[whole] != '\0') && (text[whole] != '.')) ||
        (decimal_valid(text, 0U) == 0U) || (strlen(hemisphere) != 1U))
        return 0U;
    if (latitude ? ((*hemisphere != 'N') && (*hemisphere != 'S')) :
                   ((*hemisphere != 'E') && (*hemisphere != 'W')))
        return 0U;
    for (unsigned int i = 0U; i < whole - 2U; ++i)
        degrees = degrees * 10U + (unsigned int)(text[i] - '0');
    minutes = 10U * (unsigned int)(text[whole-2U] - '0') + (unsigned int)(text[whole-1U] - '0');
    if ((degrees > (latitude ? 90U : 180U)) || (minutes >= 60U))
        return 0U;
    if (degrees == (latitude ? 90U : 180U))
    {
        if (minutes != 0U)
            return 0U;
        for (const char *p = text + whole; *p != '\0'; ++p)
            if ((*p != '.') && (*p != '0'))
                return 0U;
    }
    return 1U;
}

static uint8_t time_valid(const char *text)
{
    if ((strlen(text) < 6U) || (digits(text, 6U) == 0U) ||
        ((text[6] != '\0') && (text[6] != '.')) || (decimal_valid(text, 0U) == 0U))
        return 0U;
    return ((text[0]-'0')*10 + text[1]-'0' < 24) &&
           ((text[2]-'0')*10 + text[3]-'0' < 60) &&
           ((text[4]-'0')*10 + text[5]-'0' <= 60);
}

static uint8_t date_valid(const char *text)
{
    unsigned int day, month;
    if ((strlen(text) != 6U) || (digits(text, 6U) == 0U))
        return 0U;
    day = (unsigned int)(text[0]-'0')*10U + (unsigned int)(text[1]-'0');
    month = (unsigned int)(text[2]-'0')*10U + (unsigned int)(text[3]-'0');
    return (day >= 1U) && (day <= 31U) && (month >= 1U) && (month <= 12U);
}

static void parsed_event(lwgps_statement_t statement)
{
    parsed_statement = statement;
}

static uint8_t hex_digit(char ch)
{
    return ((ch >= '0') && (ch <= '9')) || ((ch >= 'A') && (ch <= 'F')) ||
           ((ch >= 'a') && (ch <= 'f'));
}

static uint8_t unsigned_field(const char *text, unsigned int limit, unsigned int *value)
{
    unsigned int result = 0U;
    size_t length = strlen(text);
    if ((length == 0U) || (length > 3U) || !digits(text, (unsigned int)length))
        return 0U;
    for (; *text != '\0'; ++text)
        result = result * 10U + (unsigned int)(*text - '0');
    if (result > limit)
        return 0U;
    *value = result;
    return 1U;
}

static void reset_gsv_block(void)
{
    gsv_active = gsv_good = gsv_next = 0U;
    gsv_view_count = gsv_signal_count = 0U;
    gsv_talkers_seen = 0U;
    parser.gsv_cycle_active = 0U;
}

static void finish_gsv_block(uint8_t boundary_valid)
{
    if (gsv_active == 0U)
        return;
    /* lwgps publishes all constellation series at the following non-GSV sentence. */
    if (boundary_valid && gsv_good && (gsv_next == 0U) &&
        (parser.sats_in_view == gsv_view_count))
    {
        uint8_t maximum = 0U;
        for (unsigned int i = 0U; i < gsv_view_count; ++i)
            if (parser.sats_in_view_desc[i].snr > maximum)
                maximum = parser.sats_in_view_desc[i].snr;
        data.satellites_in_view = gsv_view_count;
        data.signal_count = gsv_signal_count;
        data.signal_max_dbhz = maximum;
        data.signal_tick = gsv_last_tick;
        have_gsv = 1U;
    }
    else
        have_gsv = 0U;
    reset_gsv_block();
}

static void process_frame(void)
{
    char fields[20][13] = {{0}};
    unsigned int field_count = 1U, column = 0U;
    unsigned int star;
    uint32_t now = HAL_GetTick();
    uint8_t gga, rmc, gsa, valid, gsv = 0U;
    unsigned int gsv_messages = 0U, gsv_part = 0U, visible = 0U, reported = 0U;

    gsv = (frame_length >= 7U) && (frame[6] == ',') &&
          ((frame[1] == 'G') || (frame[1] == 'B')) &&
          (memcmp(frame+3, "GSV", 3U) == 0);
    if ((frame_length < 12U) || (frame[frame_length-2U] != '\r') ||
        (frame[frame_length-1U] != '\n'))
        goto malformed;
    star = frame_length - 5U;
    if ((frame[star] != '*') || !hex_digit(frame[star+1U]) || !hex_digit(frame[star+2U]))
        goto malformed;
    if (((frame[1] != 'G') && (frame[1] != 'B')) ||
        (frame[2] < 'A') || (frame[2] > 'Z') || (frame[6] != ','))
        return;
    gga = (memcmp(frame+3, "GGA", 3U) == 0);
    rmc = (memcmp(frame+3, "RMC", 3U) == 0);
    gsa = (memcmp(frame+3, "GSA", 3U) == 0);
    gsv = (memcmp(frame+3, "GSV", 3U) == 0);
    if (!(gga || rmc || gsa || gsv))
    {
        parsed_statement = STAT_UNKNOWN;
        lwgps_process(&parser, frame, frame_length, parsed_event);
        finish_gsv_block(parsed_statement != STAT_CHECKSUM_FAIL);
        return;
    }

    /* Bound each term before the upstream parser's fixed 13-byte term buffer. */
    for (unsigned int i = 1U; i < star; ++i)
    {
        if (frame[i] == ',')
        {
            if (++field_count > 20U)
                goto malformed;
            column = 0U;
        }
        else
        {
            if ((column >= 12U) || (frame[i] < 0x20) || (frame[i] > 0x7E))
                goto malformed;
            fields[field_count-1U][column++] = frame[i];
        }
    }
    if (gga)
    {
        if ((field_count < 15U) || (strlen(fields[6]) != 1U) ||
            (fields[6][0] < '0') || (fields[6][0] > '3') ||
            (strlen(fields[7]) == 0U) || (strlen(fields[7]) > 2U) ||
            (digits(fields[7], (unsigned int)strlen(fields[7])) == 0U))
            goto malformed;
        valid = (fields[6][0] != '0');
        if (((valid || fields[2][0] || fields[3][0]) &&
             !coordinate_valid(fields[2], fields[3], 1U)) ||
            ((valid || fields[4][0] || fields[5][0]) &&
             !coordinate_valid(fields[4], fields[5], 0U)))
            goto malformed;
        if (valid && (!time_valid(fields[1]) ||
            !decimal_valid(fields[9], 1U) || strcmp(fields[10], "M")))
            goto malformed;
    }
    else if (rmc)
    {
        if ((field_count < 10U) ||
            (strcmp(fields[2], "A") && strcmp(fields[2], "V")))
            goto malformed;
        valid = (fields[2][0] == 'A');
        if (valid && (!time_valid(fields[1]) || !date_valid(fields[9]) ||
            !coordinate_valid(fields[3], fields[4], 1U) ||
            !coordinate_valid(fields[5], fields[6], 0U)))
            goto malformed;
    }
    else if (gsa)
    {
        if ((field_count < 18U) || (strlen(fields[2]) != 1U) ||
            (fields[2][0] < '1') || (fields[2][0] > '3') ||
            !decimal_valid(fields[15], 0U) || !decimal_valid(fields[16], 0U) ||
            !decimal_valid(fields[17], 0U))
            goto malformed;
        for (unsigned int i = 3U; i <= 14U; ++i)
            if ((strlen(fields[i]) > 3U) || !digits(fields[i], (unsigned int)strlen(fields[i])))
                goto malformed;
    }

    else
    {
        unsigned int start, satellites, value;
        uint64_t talker_bit;
        if ((field_count < 4U) || !unsigned_field(fields[1], 12U, &gsv_messages) ||
            !unsigned_field(fields[2], 12U, &gsv_part) ||
            !unsigned_field(fields[3], LWGPS_CFG_SATS_IN_VIEW_SIZE, &visible) ||
            (gsv_messages == 0U) || (gsv_part == 0U) || (gsv_part > gsv_messages) ||
            (gsv_messages != ((visible == 0U) ? 1U : (visible + 3U) / 4U)))
            goto malformed;
        start = (gsv_part - 1U) * 4U;
        satellites = (visible > start) ? visible - start : 0U;
        if (satellites > 4U)
            satellites = 4U;
        if ((field_count < 4U + 4U * satellites) || ((field_count - 4U) % 4U != 0U))
            goto malformed;
        for (unsigned int i = 0U; i < satellites; ++i)
        {
            unsigned int base = 4U + 4U * i;
            if (!unsigned_field(fields[base], 255U, &value) || (value == 0U) ||
                (fields[base+1U][0] && !unsigned_field(fields[base+1U], 90U, &value)) ||
                (fields[base+2U][0] && !unsigned_field(fields[base+2U], 359U, &value)) ||
                (fields[base+3U][0] && !unsigned_field(fields[base+3U], 99U, &value)))
                goto malformed;
            if (fields[base+3U][0] != '\0')
                ++reported;
        }
        for (unsigned int i = 4U + 4U * satellites; i < field_count; ++i)
            if (fields[i][0] != '\0')
                goto malformed;
        talker_bit = (uint64_t)1U << ((frame[1] == 'B' ? 26U : 0U) +
                                     (unsigned int)(frame[2] - 'A'));
        if (!gsv_active || ((gsv_part == 1U) &&
            ((gsv_talkers_seen & talker_bit) != 0U)))
        {
            reset_gsv_block();
            gsv_active = gsv_good = 1U;
            gsv_start_tick = now;
        }
        if (gsv_part == 1U)
        {
            if (gsv_next != 0U)
                gsv_good = 0U;
            gsv_talkers_seen |= talker_bit;
            gsv_talker[0] = frame[1];
            gsv_talker[1] = frame[2];
            gsv_next = 1U;
            gsv_total = (uint8_t)gsv_messages;
            gsv_visible = (uint8_t)visible;
            if (gsv_view_count + visible > LWGPS_CFG_SATS_IN_VIEW_SIZE)
                gsv_good = 0U;
        }
        if ((gsv_next != gsv_part) || (gsv_talker[0] != frame[1]) ||
            (gsv_talker[1] != frame[2]) || (gsv_total != gsv_messages) ||
            (gsv_visible != visible))
            gsv_good = 0U;
    }

    parsed_statement = STAT_UNKNOWN;
    lwgps_process(&parser, frame, frame_length, parsed_event);
    if (!gsv)
        finish_gsv_block(parsed_statement != STAT_CHECKSUM_FAIL);
    if (parsed_statement == STAT_CHECKSUM_FAIL)
    {
        ++data.checksum_errors;
        if (gsv)
        {
            gsv_good = have_gsv = 0U;
        }
        return;
    }
    if (gsv && (parsed_statement == STAT_GSV))
    {
        ++data.gsv_count;
        if (gsv_good)
        {
            gsv_signal_count = (uint8_t)(gsv_signal_count + reported);
            gsv_last_tick = now;
            if (gsv_part == gsv_messages)
                gsv_view_count = (uint8_t)(gsv_view_count + visible);
        }
        gsv_next = (gsv_part == gsv_messages) ? 0U : (uint8_t)(gsv_part + 1U);
    }
    else if (gga && (parsed_statement == STAT_GGA))
    {
        if (parser.fix && ((parser.altitude < -10000.0) || (parser.altitude > 100000.0)))
            goto malformed;
        ++data.gga_count;
        gga_tick = now;
        gga_valid = (parser.fix != 0U);
        data.satellites = parser.sats_in_use;
        if (gga_valid)
        {
            candidate_latitude = rounded(parser.latitude * 1000000.0);
            candidate_longitude = rounded(parser.longitude * 1000000.0);
            candidate_altitude = rounded(parser.altitude * 100.0);
            ++candidate_sequence;
        }
    }
    else if (rmc && (parsed_statement == STAT_RMC))
    {
        ++data.rmc_count;
        rmc_tick = now;
        rmc_valid = parser.is_valid;
        data.utc_valid = time_valid(fields[1]) && date_valid(fields[9]);
        if (data.utc_valid)
        {
            data.utc_hour = parser.hours;
            data.utc_minute = parser.minutes;
            data.utc_second = parser.seconds;
            data.utc_day = parser.date;
            data.utc_month = parser.month;
            data.utc_year = parser.year;
        }
    }
    else if (gsa && (parsed_statement == STAT_GSA))
    {
        if ((parser.dop_h < 0.0) || (parser.dop_h > 999.9))
            goto malformed;
        data.hdop_x100 = (uint32_t)rounded(parser.dop_h * 100.0);
        have_hdop = 1U;
        hdop_tick = now;
    }
    return;

malformed:
    ++data.bad_frames;
    if (gsv)
        gsv_good = have_gsv = 0U;
}

void BSP_GPS_Init(void)
{
    rx_head = rx_tail = 0U;
    rx_bytes = dropped_bytes = uart_errors = uart_error_mask = fault_epoch = 0U;
    handled_fault_epoch = 0U;
    frame_length = 0U;
    memset(&data, 0, sizeof(data));
    lwgps_init(&parser);
    have_gsv = 0U;
    reset_gsv_block();
    gga_valid = rmc_valid = have_hdop = 0U;
    candidate_sequence = committed_sequence = 0U;
}

void BSP_GPS_RxByte(uint8_t byte)
{
    uint16_t next = (uint16_t)((rx_head + 1U) % GPS_RING_SIZE);
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

void BSP_GPS_RxError(uint32_t error)
{
    ++uart_errors;
    uart_error_mask |= error;
    ++fault_epoch;
}

void BSP_GPS_Poll(void)
{
    uint32_t now;

    if (handled_fault_epoch != fault_epoch)
    {
        handled_fault_epoch = fault_epoch;
        /* Discard a broken stream so pre-loss fragments cannot form a new fix. */
        rx_tail = rx_head;
        frame_length = 0U;
        gga_valid = rmc_valid = have_hdop = 0U;
        data.utc_valid = 0U;
        have_gsv = 0U;
        reset_gsv_block();
    }
    for (unsigned int budget = 0U; (budget < 128U) && (rx_tail != rx_head); ++budget)
    {
        uint8_t byte = rx_ring[rx_tail];
        rx_tail = (uint16_t)((rx_tail + 1U) % GPS_RING_SIZE);
        if (byte == '$')
        {
            frame[0] = '$';
            frame_length = 1U;
        }
        else if (frame_length != 0U)
        {
            if (frame_length >= sizeof(frame) - 1U)
            {
                ++data.bad_frames;
                if (memcmp(frame+3, "GSV", 3U) == 0)
                    gsv_good = have_gsv = 0U;
                frame_length = 0U;
                continue;
            }
            frame[frame_length++] = (char)byte;
            if (byte == '\n')
            {
                frame[frame_length] = '\0';
                if (handled_fault_epoch == fault_epoch)
                    process_frame();
                frame_length = 0U;
            }
        }
    }
    now = HAL_GetTick();
    if ((uint32_t)(now - gga_tick) >= GPS_STALE_MS)
        gga_valid = 0U;
    if ((uint32_t)(now - rmc_tick) >= GPS_STALE_MS)
    {
        rmc_valid = 0U;
        data.utc_valid = 0U;
    }
    if ((uint32_t)(now - hdop_tick) >= GPS_STALE_MS)
        have_hdop = 0U;
    data.fix = gga_valid && rmc_valid && (handled_fault_epoch == fault_epoch);
    if (data.fix && (candidate_sequence != committed_sequence))
    {
        data.latitude_udeg = candidate_latitude;
        data.longitude_udeg = candidate_longitude;
        data.altitude_cm = candidate_altitude;
        data.position_tick = gga_tick;
        data.has_position = 1U;
        committed_sequence = candidate_sequence;
    }
    data.age_ms = data.has_position ? (uint32_t)(now - data.position_tick) : 0U;
    if (gsv_active && ((uint32_t)(now - gsv_start_tick) >= GPS_STALE_MS))
        reset_gsv_block();
    if ((uint32_t)(now - data.signal_tick) >= GPS_STALE_MS)
        have_gsv = 0U;
    data.gsv_valid = have_gsv && (handled_fault_epoch == fault_epoch);
    data.signal_valid = data.gsv_valid && (data.signal_count != 0U);
    data.signal_level = BSP_GPS_SIGNAL_NONE;
    if (data.signal_valid && (data.signal_max_dbhz > 0U))
    {
        if (data.signal_max_dbhz >= 40U)
            data.signal_level = BSP_GPS_SIGNAL_STRONG;
        else if (data.signal_max_dbhz >= 30U)
            data.signal_level = BSP_GPS_SIGNAL_MEDIUM;
        else
            data.signal_level = BSP_GPS_SIGNAL_WEAK;
    }
    data.signal_age_ms = data.gsv_valid ? (uint32_t)(now - data.signal_tick) : 0U;
    data.hdop_valid = have_hdop && ((uint32_t)(now - hdop_tick) < GPS_STALE_MS);
    data.rx_bytes = rx_bytes;
    data.dropped_bytes = dropped_bytes;
    data.uart_errors = uart_errors;
    data.uart_error_mask = uart_error_mask;
}

const bsp_gps_data_t *BSP_GPS_GetData(void)
{
    return &data;
}
