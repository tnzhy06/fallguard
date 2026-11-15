#include "app_ui.h"
#include "app_audio.h"
#include "app_fall.h"
#include "app_report.h"
#include "app_ui_icons.h"
#include "bsp_esp.h"
#include "bsp_gps.h"
#include "bsp_lcd.h"
#include "bsp_mpu6050.h"
#include "bsp_rtc.h"
#include "bsp_log.h"
#include "st7789.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UI_PAGE_COUNT 5U
#define UI_SIGNAL_BARS 4U
#define BG LCD_RGB(245, 245, 247)
#define CARD LCD_RGB(255, 255, 255)
#define INK LCD_RGB(29, 29, 31)
#define MUTED LCD_RGB(118, 118, 128)
#define EDGE LCD_RGB(213, 213, 219)
#define BLUE LCD_RGB(0, 122, 255)
#define GREEN LCD_RGB(36, 150, 78)
#define AMBER LCD_RGB(190, 105, 0)
#define RED LCD_RGB(230, 48, 55)
#define RED_BG LCD_RGB(255, 235, 237)

#define BOOT_LOG_LINES 5U
#define BOOT_LOG_TEXT_BYTES 64U
typedef struct {
  char text[BOOT_LOG_TEXT_BYTES];
  app_ui_boot_level_t level;
} boot_line_t;
static boot_line_t boot_lines[BOOT_LOG_LINES];
static uint8_t boot_count, boot_active, boot_hardware_done, boot_finishing;
static uint32_t boot_sensor_tick, boot_finish_tick;
static bsp_mpu6050_state_t boot_mpu_state;

static uint8_t page, alarm_active, dirty, rendering;
static uint16_t next_y;
static uint32_t last_refresh;
static st7789_state_t last_lcd_state;

/* One immutable snapshot per frame: values cannot change between strips. */
static struct {
  bsp_rtc_data_t rtc;
  bsp_gps_data_t gps;
  bsp_mpu6050_sample_t mpu;
  bsp_mpu6050_state_t mpu_state;
  bsp_esp_data_t esp;
  app_report_data_t report;
  app_fall_data_t fall;
  app_audio_state_t audio;
  uint8_t page, alarm, online;
  uint32_t text_page;
  uint8_t boot, boot_count, boot_finishing;
  boot_line_t boot_lines[BOOT_LOG_LINES];
} frame;

static void text(int x, int y, const char *s, uint16_t color, uint16_t bg) {
  BSP_LCD_Text(x, y, s, 16U, 1U, color, bg);
}

static void small(int x, int y, const char *s, uint16_t color, uint16_t bg) {
  BSP_LCD_Text(x, y, s, 14U, 1U, color, bg);
}

static void title(int x, int y, const char *s, uint16_t color, uint16_t bg) {
  BSP_LCD_Text(x, y, s, 20U, 1U, color, bg);
}

static void center(int y, const char *s, uint8_t size, uint8_t scale,
                   uint16_t color, uint16_t bg) {
  BSP_LCD_Text((240 - BSP_LCD_TextWidth(s, size, scale)) / 2, y, s, size, scale, color, bg);
}

static void right(int x, int y, const char *s, uint8_t size, uint16_t color, uint16_t bg) {
  BSP_LCD_Text(x - BSP_LCD_TextWidth(s, size, 1U), y, s, size, 1U, color, bg);
}

/* Fit the complete value, or page it at complete UTF-8 glyph boundaries. */
static void value_line(int x, int y, int width, const char *s, uint8_t size,
                        uint16_t color, uint16_t bg) {
  if (size == 20U && BSP_LCD_TextWidth(s, size, 1U) > width) size = 16U;
  if (size == 16U && BSP_LCD_TextWidth(s, size, 1U) > width) size = 14U;
  BSP_LCD_TextPage(x, y, width, s, size, frame.text_page, color, bg);
}

static uint16_t signal_bar_color(unsigned bar) {
  return frame.gps.signal_valid && frame.gps.signal_level >= bar ? BLUE : EDGE;
}

static void header(const char *s) {
  title(12, 8, s, INK, BG);
  const int x = 144, y = 10, pitch = 30;
  BSP_LCD_Icon(x, y, ui_wifi_icon, UI_STATUS_ICON_SIZE,
               frame.esp.wifi_connected ? BLUE : EDGE, BG);
  BSP_LCD_Icon(x + pitch, y, ui_mqtt_icon, UI_STATUS_ICON_SIZE,
               frame.online ? GREEN : EDGE, BG);
  for (unsigned i = 0; i < UI_SIGNAL_BARS; ++i)
    BSP_LCD_Icon(x + 2 * pitch, y, ui_signal_icons[i], UI_STATUS_ICON_SIZE, signal_bar_color(i), BG);
  BSP_LCD_Rect(12, 40, 216, 1, EDGE);
}

static void footer(void) {
  BSP_LCD_Rect(12, 208, 216, 1, EDGE);
  small(12, 212, "K1 翻页", MUTED, BG);
  right(228, 212, "K2 测试", 14U, MUTED, BG);
  for (unsigned i = 0; i < UI_PAGE_COUNT; ++i)
    BSP_LCD_RoundRect(98 + (int)i * 10, 220, frame.page == i ? 6 : 4, 4, 2,
                      frame.page == i ? BLUE : EDGE);
}

static const char *gps_status(void) {
  return frame.gps.fix ? "定位成功" : (frame.gps.has_position ? "历史位置" : "等待定位");
}

static uint8_t mpu_ready(void) {
  return frame.mpu_state == BSP_MPU6050_READY && frame.mpu.samples != 0U;
}

static const char *fall_status(void) {
  if (frame.mpu_state == BSP_MPU6050_ERROR) return "传感故障";
  if (frame.mpu_state == BSP_MPU6050_CALIBRATING) return "静置校准";
  if (!mpu_ready()) return "正在启动";
  if (frame.fall.state == APP_FALL_WAIT_UPRIGHT) return "请扶正设备";
  if (frame.fall.state == APP_FALL_HOLDING) return "注意倾斜";
  return "监测正常";
}

static void draw_boot(void) {
  title(12, 8, "跌倒守护", INK, BG);
  small(12, 38, "启动日志", MUTED, BG);
  BSP_LCD_RoundRect(12, 62, 216, 122, 14, CARD);
  for (unsigned i = 0U; i < frame.boot_count; ++i) {
    const boot_line_t *line = &frame.boot_lines[i];
    int y = 68 + (int)i * 22;
    uint16_t color = line->level == APP_UI_BOOT_WARN ? AMBER :
                     (line->level == APP_UI_BOOT_WAIT ? BLUE : GREEN);
    BSP_LCD_Circle(22, y + 10, 3, color);
    value_line(32, y, 184, line->text, 14U,
                line->level == APP_UI_BOOT_WARN ? AMBER : INK, CARD);
  }
  center(188, frame.boot_finishing ? "初始化完成" : "保持静止 正在启动",
         14U, 1U, frame.boot_finishing ? BLUE : MUTED, BG);
  center(212, "HarmonyOS Sans", 14U, 1U, MUTED, BG);
}

static void draw_home(void) {
  char s[48];
  uint8_t safe = mpu_ready() && frame.fall.state == APP_FALL_IDLE;
  uint16_t accent = safe ? GREEN : (frame.mpu_state == BSP_MPU6050_ERROR ? RED : AMBER);
  header("跌倒守护");
  if (frame.rtc.valid)
    snprintf(s, sizeof(s), "%04u/%02u/%02u", (unsigned)frame.rtc.local.year,
             (unsigned)frame.rtc.local.month, (unsigned)frame.rtc.local.day);
  else
    snprintf(s, sizeof(s), "%s", frame.rtc.error ? "时间故障" : "等待时间同步");
  small(12, 48, s, frame.rtc.error ? AMBER : MUTED, BG);
  snprintf(s, sizeof(s), "UTC%+d", APP_TIMEZONE_HOURS);
  right(228, 48, s, 14U, MUTED, BG);
  if (frame.rtc.valid)
    snprintf(s, sizeof(s), "%02u:%02u", (unsigned)frame.rtc.local.hour, (unsigned)frame.rtc.local.minute);
  else snprintf(s, sizeof(s), "--:--");
  BSP_LCD_Clock(12, 72, s, INK, BG);
  if (frame.rtc.valid) snprintf(s, sizeof(s), "%02u", (unsigned)frame.rtc.local.second);
  else snprintf(s, sizeof(s), "--");
  right(228, 90, s, 20U, BLUE, BG);
  BSP_LCD_RoundRect(12, 130, 216, 54, 14, CARD);
  text(24, 134, fall_status(), accent, CARD);
  small(24, 158, safe ? "持续监测中" : "请检查设备状态", MUTED, CARD);
  if (safe) {
    BSP_LCD_Circle(204, 153, 10, GREEN);
    BSP_LCD_Line(198, 153, 202, 157, CARD);
    BSP_LCD_Line(202, 157, 210, 148, CARD);
  }
  small(12, 188, gps_status(), frame.gps.fix ? BLUE : MUTED, BG);
  right(228, 188, frame.online ? "云端在线" : "云端离线", 14U, frame.online ? GREEN : AMBER, BG);
}

static uint32_t magnitude(int32_t value) {
  return value < 0 ? 0U - (uint32_t)value : (uint32_t)value;
}

static void coordinate(char *s, size_t n, int32_t value, uint8_t valid, uint8_t lon) {
  uint32_t m = magnitude(value);
  if (!valid) snprintf(s, n, "--.------");
  else snprintf(s, n, "%lu.%06lu %c", (unsigned long)(m / 1000000U),
                 (unsigned long)(m % 1000000U), lon ? (value < 0 ? 'W' : 'E') : (value < 0 ? 'S' : 'N'));
}

static void draw_position(void) {
  char s[48];
  header("位置坐标");
  for (unsigned i = 0; i < 2U; ++i) {
    int y = 48 + (int)i * 66;
    BSP_LCD_RoundRect(12, y, 216, 60, 14, CARD);
    small(24, y + 4, i ? "经度" : "纬度", MUTED, CARD);
    coordinate(s, sizeof(s), i ? frame.gps.longitude_udeg : frame.gps.latitude_udeg,
               frame.gps.has_position, i);
    value_line(24, y + 28, 192, s, 16U, BLUE, CARD);
  }
  small(12, 184, gps_status(), frame.gps.fix ? GREEN : AMBER, BG);
  if (frame.gps.has_position) {
    uint32_t age = frame.gps.age_ms / 1000U;
    snprintf(s, sizeof(s), "%lus 前", (unsigned long)age);
    right(228, 184, s, 14U, MUTED, BG);
  }
}

static void draw_gps(void) {
  char s[64];
  const bsp_gps_data_t *g = &frame.gps;
  header("卫星信号");
  BSP_LCD_RoundRect(12, 48, 216, 56, 14, CARD);
  text(24, 52, gps_status(), g->fix ? GREEN : AMBER, CARD);
  if (g->signal_valid) snprintf(s, sizeof(s), "C/N0 %u dBHz", (unsigned)g->signal_max_dbhz);
  else snprintf(s, sizeof(s), "C/N0 --");
  value_line(24, 78, 136, s, 14U, INK, CARD);
  /* A 44x32 indicator centered vertically in the card, clear of the text. */
  for (unsigned i = 0; i < UI_SIGNAL_BARS; ++i) {
    int h = 8 + (int)i * 8;
    BSP_LCD_RoundRect(172 + (int)i * 12, 92 - h, 8, h, 2, signal_bar_color(i));
  }
  snprintf(s, sizeof(s), "卫星 %u   可见 %u", (unsigned)g->satellites, (unsigned)g->satellites_in_view);
  value_line(12, 110, 216, s, 14U, INK, BG);
  if (g->has_position) {
    uint32_t m = magnitude(g->altitude_cm);
    snprintf(s, sizeof(s), "海拔 %c%lu.%02lu m", g->altitude_cm < 0 ? '-' : '+',
             (unsigned long)(m / 100U), (unsigned long)(m % 100U));
  } else snprintf(s, sizeof(s), "海拔 --");
  value_line(12, 132, 216, s, 14U, INK, BG);
  if (g->hdop_valid)
    snprintf(s, sizeof(s), "精度 HDOP %lu.%02lu", (unsigned long)(g->hdop_x100 / 100U),
             (unsigned long)(g->hdop_x100 % 100U));
  else snprintf(s, sizeof(s), "精度 HDOP --");
  value_line(12, 154, 216, s, 14U, MUTED, BG);
  if (g->utc_valid)
    snprintf(s, sizeof(s), "UTC %02u:%02u:%02u %02u/%02u", (unsigned)g->utc_hour,
             (unsigned)g->utc_minute, (unsigned)g->utc_second, (unsigned)g->utc_month, (unsigned)g->utc_day);
  else snprintf(s, sizeof(s), "UTC 等待时间数据");
  value_line(12, 182, 216, s, 14U, MUTED, BG);
}

static void signed_value(char *s, size_t n, int32_t value, uint32_t divisor) {
  uint32_t m = magnitude(value);
  snprintf(s, n, "%c%lu.%02lu", value < 0 ? '-' : '+', (unsigned long)(m / divisor),
           (unsigned long)((m % divisor) * 100U / divisor));
}

static void draw_motion(void) {
  char s[64], value[32];
  header("姿态监测");
  if (!mpu_ready()) {
    BSP_LCD_RoundRect(12, 52, 216, 138, 14, CARD);
    center(78, fall_status(), 20U, 1U, AMBER, CARD);
    center(126, frame.mpu_state == BSP_MPU6050_ERROR ? "请检查传感器接线" : "保持静止约三秒",
           14U, 1U, MUTED, CARD);
    return;
  }
  for (unsigned i = 0; i < 2U; ++i) {
    int x = 12 + (int)i * 112;
    uint16_t accent = magnitude(frame.mpu.angle_cdeg[i]) >= APP_FALL_ANGLE_DEG * 100U ? AMBER : BLUE;
    BSP_LCD_RoundRect(x, 48, 104, 50, 12, CARD);
    small(x + 8, 50, i ? "俯仰角" : "横滚角", MUTED, CARD);
    signed_value(s, sizeof(s), frame.mpu.angle_cdeg[i], 100U);
    value_line(x + 8, 72, 88, s, 16U, accent, CARD);
  }
  small(12, 100, "加速度 g", MUTED, BG);
  small(126, 100, "角速度 d/s", MUTED, BG);
  for (unsigned i = 0; i < 3U; ++i) {
    signed_value(value, sizeof(value), frame.mpu.accel_mg[i], 1000U);
    snprintf(s, sizeof(s), "%c %s", 'X' + i, value);
    value_line(12, 122 + (int)i * 21, 100, s, 14U, INK, BG);
    signed_value(value, sizeof(value), frame.mpu.gyro_mdps[i], 1000U);
    snprintf(s, sizeof(s), "%c %s", 'X' + i, value);
    value_line(126, 122 + (int)i * 21, 102, s, 14U, INK, BG);
  }
  if (APP_FALL_HOLD_MS % 1000U == 0U)
    snprintf(s, sizeof(s), "触发 %u度 / %lus", (unsigned)APP_FALL_ANGLE_DEG, (unsigned long)(APP_FALL_HOLD_MS / 1000U));
  else snprintf(s, sizeof(s), "触发 %u度 / %lu.%03lus", (unsigned)APP_FALL_ANGLE_DEG,
                 (unsigned long)(APP_FALL_HOLD_MS / 1000U), (unsigned long)(APP_FALL_HOLD_MS % 1000U));
  value_line(12, 188, 216, s, 14U, AMBER, BG);
}

static const char *network_stage(void) {
  if (frame.online) return "数据上报已就绪";
  switch (frame.esp.state) {
  case BSP_ESP_SNTP_CONFIG: case BSP_ESP_SNTP_QUERY: return "正在同步时间";
  case BSP_ESP_NO_CONFIG: return "请配置网络参数";
  case BSP_ESP_UNSUPPORTED: return "模块固件不支持";
  case BSP_ESP_BACKOFF: case BSP_ESP_RESET_LOW: return "等待重新连接";
  case BSP_ESP_MQTT_CLEAN: case BSP_ESP_MQTT_USER: case BSP_ESP_MQTT_CONFIG:
  case BSP_ESP_MQTT_CONNECT: case BSP_ESP_MQTT_SUBSCRIBE:
  case BSP_ESP_MQTT_SUBSCRIBE_QUERY: return "正在连接云端";
  default: return "正在连接网络";
  }
}

static void draw_network(void) {
  char s[48];
  header("网络服务");
  BSP_LCD_RoundRect(12, 48, 216, 54, 14, CARD);
  text(24, 52, frame.online ? "云端在线" : "云端离线", frame.online ? GREEN : AMBER, CARD);
  small(24, 78, network_stage(), MUTED, CARD);
  small(12, 110, "设备编号", MUTED, BG);
  value_line(96, 110, 132, APP_DEVICE_ID, 14U, INK, BG);
  small(12, 136, "上报周期", MUTED, BG);
  snprintf(s, sizeof(s), "%lu 秒", (unsigned long)frame.report.interval_seconds);
  value_line(96, 136, 132, s, 14U, INK, BG);
  small(12, 162, "发送成功", MUTED, BG);
  snprintf(s, sizeof(s), "%lu", (unsigned long)frame.esp.published);
  value_line(96, 162, 132, s, 14U, BLUE, BG);
  small(12, 188, frame.esp.wifi_connected ? "无线已连接" : "无线未连接", MUTED, BG);
}

static void draw_alarm(void) {
  uint8_t automatic = frame.fall.source == APP_FALL_SOURCE_AUTO;
  BSP_LCD_RoundRect(12, 12, 216, 28, 10, RED_BG);
  center(16, automatic ? "检测到跌倒" : "手动测试报警", 14U, 1U, RED, RED_BG);
  BSP_LCD_Circle(120, 72, 18, RED_BG);
  BSP_LCD_RoundRect(118, 59, 4, 16, 2, RED);
  BSP_LCD_Circle(120, 82, 2, RED);
  center(98, automatic ? "跌倒报警" : "测试报警", 40U, 1U, INK, BG);
  center(148, frame.audio == APP_AUDIO_PLAYING ? "语音求助播放中" :
          (frame.audio == APP_AUDIO_ERROR ? "语音故障请检查" : "语音未播放"), 14U, 1U, MUTED, BG);
  small(18, 176, frame.online ? "云端在线" : "云端离线", frame.online ? GREEN : AMBER, BG);
  right(222, 176, gps_status(), 14U, frame.gps.fix ? BLUE : AMBER, BG);
  BSP_LCD_RoundRect(12, 204, 216, 28, 10, RED);
  center(208, "按 K2 取消报警", 14U, 1U, BG, RED);
}

static void snapshot(void) {
  frame.text_page = HAL_GetTick() / APP_UI_TEXT_PAGE_MS;
  frame.rtc = *BSP_RTC_GetData();
  frame.gps = *BSP_GPS_GetData();
  frame.mpu = *BSP_MPU6050_GetSample();
  frame.mpu_state = BSP_MPU6050_GetState();
  frame.esp = *BSP_ESP_GetData();
  frame.report = *app_report_get_data();
  frame.fall = *app_fall_get_data();
  frame.audio = app_audio_get_state();
  frame.online = BSP_ESP_IsOnline();
  frame.page = page;
  frame.alarm = alarm_active;
  frame.boot = boot_active && !alarm_active;
  frame.boot_count = boot_count;
  frame.boot_finishing = boot_finishing;
  memcpy(frame.boot_lines, boot_lines, sizeof(boot_lines));
}

void app_ui_init(void) {
  page = alarm_active = rendering = 0U;
  dirty = 1U;
  next_y = 0U;
  last_refresh = HAL_GetTick();
  last_lcd_state = ST7789_WAIT_POWER;
  ST7789_Init();
  boot_active = 1U;
  boot_count = boot_hardware_done = boot_finishing = 0U;
  boot_mpu_state = BSP_MPU6050_STARTING;
  memset(boot_lines, 0, sizeof(boot_lines));
}

void app_ui_boot_log(const char *message, app_ui_boot_level_t level) {
  if (!boot_active || !message)
    return;
  if (boot_count == BOOT_LOG_LINES) {
    memmove(boot_lines, boot_lines + 1U, sizeof(boot_lines) - sizeof(boot_lines[0]));
    --boot_count;
  }
  snprintf(boot_lines[boot_count].text, BOOT_LOG_TEXT_BYTES, "%s", message);
  boot_lines[boot_count++].level = level;
  dirty = 1U;
  DEBUG_PRINT(DEBUG_LOG_BOOT, "[BOOT] %s\r\n", message);
}

void app_ui_boot_hardware_done(void) {
  boot_hardware_done = 1U;
  boot_sensor_tick = HAL_GetTick();
  app_ui_boot_log("姿态传感器初始化", APP_UI_BOOT_WAIT);
}

uint8_t app_ui_boot_active(void) { return boot_active; }

uint8_t app_ui_frame_pending(void) {
  return ST7789_GetState() != ST7789_ERROR &&
         (dirty || rendering || ST7789_IsBusy() || ST7789_GetState() != ST7789_READY);
}

static void boot_poll(uint32_t now) {
  if (!boot_active || !boot_hardware_done)
    return;
  if (boot_finishing) {
    if ((uint32_t)(now - boot_finish_tick) >= APP_BOOT_COMPLETE_HOLD_MS) {
      boot_active = 0U;
      dirty = 1U;
    }
    return;
  }
  bsp_mpu6050_state_t state = BSP_MPU6050_GetState();
  if (state == BSP_MPU6050_CALIBRATING && state != boot_mpu_state)
    app_ui_boot_log("姿态传感器校准中", APP_UI_BOOT_WAIT);
  boot_mpu_state = state;
  if (state == BSP_MPU6050_READY || state == BSP_MPU6050_ERROR ||
      (uint32_t)(now - boot_sensor_tick) >= APP_BOOT_SENSOR_TIMEOUT_MS) {
    app_ui_boot_log(state == BSP_MPU6050_READY ? "姿态校准完成" :
                    (state == BSP_MPU6050_ERROR ? "姿态故障 降级运行" : "姿态超时 降级运行"),
                    state == BSP_MPU6050_READY ? APP_UI_BOOT_OK : APP_UI_BOOT_WARN);
    boot_finishing = 1U;
    boot_finish_tick = now;
  }
}

void app_ui_next_page(void) {
  if (alarm_active || boot_active)
    return;
  page = (uint8_t)((page + 1U) % UI_PAGE_COUNT);
  dirty = 1U;
}

void app_ui_set_alarm(uint8_t active) {
  if (alarm_active == active)
    return;
  alarm_active = active;
  if (active)
    boot_active = 0U; /* A real/manual alarm always takes precedence over the boot log. */
  if (!active)
    page = 0U;
  dirty = 1U;
}

void app_ui_poll(void) {
  uint32_t now = HAL_GetTick();
  boot_poll(now);
  ST7789_Poll();
  st7789_state_t state = ST7789_GetState();
  if (state != last_lcd_state) {
    last_lcd_state = state;
    if (state == ST7789_READY) {
      dirty = 1U;
      rendering = 0U;
      DEBUG_PRINT(DEBUG_LOG_STATUS,
                  "[LCD] SPI controller configured; first frame pending\r\n");
    } else if (state == ST7789_ERROR)
      DEBUG_PRINT(DEBUG_LOG_ERRORS, "[LCD] SPI/DMA error; retry in %lu ms\r\n",
                  (unsigned long)LCD_RETRY_MS);
  }
  if ((state != ST7789_READY) || ST7789_IsBusy())
    return;
  if (dirty ||
      (!rendering && (uint32_t)(now - last_refresh) >= APP_UI_REFRESH_MS)) {
    snapshot();
    next_y = 0U;
    dirty = 0U;
    rendering = 1U;
    last_refresh = now;
  }
  if (!rendering)
    return;
  if (next_y >= ST7789_HEIGHT) {
    ST7789_DisplayOn();
    rendering = 0U;
    return;
  }
  BSP_LCD_BeginBand(next_y, BG);
  if (frame.boot)
    draw_boot();
  else if (frame.alarm)
    draw_alarm();
  else {
    switch (frame.page) {
    case 0U:
      draw_home();
      break;
    case 1U:
      draw_position();
      break;
    case 2U:
      draw_gps();
      break;
    case 3U:
      draw_motion();
      break;
    default:
      draw_network();
      break;
    }
    footer();
  }
  if (ST7789_WriteRows(next_y, LCD_BAND_ROWS, BSP_LCD_GetBand()))
    next_y += LCD_BAND_ROWS;
}
