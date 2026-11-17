#include "app_report.h"
#include "app_fall.h"
#include "app_json.h"
#include "bsp_esp.h"
#include "bsp_gps.h"
#include "bsp_rtc.h"
#include "bsp_log.h"
#include "stm32f1xx_hal.h"

static app_report_data_t data;
static struct {
  app_json_snapshot_t snapshot;
  uint8_t fall;
} events[APP_REPORT_EVENT_QUEUE_SIZE];
static uint8_t event_head, event_tail, event_count, last_online, last_alarm,
    location_due;
static uint32_t location_tick;

static app_json_snapshot_t snapshot(void) {
  const bsp_gps_data_t *gps = BSP_GPS_GetData();
  const bsp_rtc_data_t *rtc = BSP_RTC_GetData();
  app_json_snapshot_t value;
  value.ts = rtc->valid ? rtc->unix_utc : 0U;
  value.latitude = gps->has_position ? gps->latitude_udeg : 0;
  value.longitude = gps->has_position ? gps->longitude_udeg : 0;
  value.fix = gps->fix ? 1U : 0U;
  value.alarm = app_fall_get_data()->state == APP_FALL_ALARM;
  return value;
}

void app_report_init(void) {
  data.interval_seconds = APP_REPORT_DEFAULT_SECONDS;
  data.bad_commands = data.dropped_events = 0U;
  event_head = event_tail = event_count = 0U;
  last_online = location_due = 0U;
  last_alarm = app_fall_get_data()->state == APP_FALL_ALARM;
  location_tick = HAL_GetTick();
}

static void record_alarm_change(uint8_t alarm) {
  if (alarm == last_alarm)
    return;
  if (event_count < APP_REPORT_EVENT_QUEUE_SIZE) {
    events[event_head].snapshot = snapshot();
    events[event_head].fall = alarm;
    event_head = (uint8_t)((event_head + 1U) % APP_REPORT_EVENT_QUEUE_SIZE);
    ++event_count;
  } else {
    ++data.dropped_events;
    LOG_WARN(DEBUG_LOG_REPORT_ENABLE, LOG_TAG_REPORT,
             "online event queue full\r\n");
  }
  last_alarm = alarm;
  location_due = 1U;
}

void app_report_poll(void) {
  char payload[BSP_ESP_PAYLOAD_SIZE];
  app_json_command_t command;
  uint32_t now = HAL_GetTick();
  uint8_t online = BSP_ESP_IsOnline();
  uint8_t alarm = app_fall_get_data()->state == APP_FALL_ALARM;

  if (!online) {
    event_head = event_tail = event_count = location_due = 0U;
    last_alarm = alarm;
    last_online = 0U;
    return;
  }
  if (!last_online) {
    location_due = 1U;
    last_alarm = alarm;
    LOG_INFO(DEBUG_LOG_REPORT_ENABLE, LOG_TAG_REPORT,
             "online; report current state, no offline replay\r\n");
  } else
    record_alarm_change(alarm);
  last_online = 1U;
  for (uint8_t budget = 0U; (budget < APP_REPORT_COMMAND_BUDGET) &&
                            BSP_ESP_ReadCommand(payload, sizeof(payload));
       ++budget) {
    if (!app_json_command(payload, &command)) {
      ++data.bad_commands;
      LOG_WARN(DEBUG_LOG_REPORT_ENABLE, LOG_TAG_REPORT,
               "invalid command rejected\r\n");
      continue;
    }
    if (command.type == APP_CMD_LOC) {
      location_due = 1U;
      LOG_INFO(DEBUG_LOG_REPORT_ENABLE, LOG_TAG_REPORT,
               "immediate location requested\r\n");
    } else if (command.type == APP_CMD_CLEAR) {
      /* Remote clear is idempotent; a repeated command must never create an
       * alarm. */
      if (app_fall_get_data()->state == APP_FALL_ALARM)
        app_fall_toggle_alarm();
      record_alarm_change(app_fall_get_data()->state == APP_FALL_ALARM);
      LOG_INFO(DEBUG_LOG_REPORT_ENABLE, LOG_TAG_REPORT,
               "remote clear requested\r\n");
    } else {
      data.interval_seconds = command.seconds;
      location_tick = now;
      LOG_INFO(DEBUG_LOG_REPORT_ENABLE, LOG_TAG_REPORT,
               "interval=%lu seconds\r\n", (unsigned long)command.seconds);
    }
  }
  if ((uint32_t)(now - location_tick) >= data.interval_seconds * 1000U)
    location_due = 1U;
  if (event_count) {
    if (app_json_event(payload, sizeof(payload), &events[event_tail].snapshot,
                       events[event_tail].fall) &&
        BSP_ESP_Publish(BSP_ESP_TOPIC_ALARM, payload)) {
      LOG_INFO(DEBUG_LOG_REPORT_ENABLE, LOG_TAG_REPORT, "%s\r\n", payload);
      event_tail = (uint8_t)((event_tail + 1U) % APP_REPORT_EVENT_QUEUE_SIZE);
      --event_count;
    }
  } else if (location_due) {
    app_json_snapshot_t value = snapshot();
    if (app_json_location(payload, sizeof(payload), &value) &&
        BSP_ESP_Publish(BSP_ESP_TOPIC_LOC, payload)) {
      LOG_INFO(DEBUG_LOG_REPORT_ENABLE, LOG_TAG_REPORT, "%s\r\n", payload);
      location_tick = now;
      location_due = 0U;
    }
  }
}

const app_report_data_t *app_report_get_data(void) { return &data; }
