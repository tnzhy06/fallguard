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

#define UI_PAGE_COUNT 5U
#define UI_SIGNAL_BARS 4U
#define BG LCD_RGB(10, 17, 29)
#define CARD LCD_RGB(22, 34, 51)
#define WHITE LCD_RGB(237, 244, 250)
#define MUTED LCD_RGB(143, 163, 186)
#define EDGE LCD_RGB(42, 59, 79)
#define CYAN LCD_RGB(69, 204, 232)
#define GREEN LCD_RGB(71, 222, 169)
#define AMBER LCD_RGB(255, 195, 91)
#define RED LCD_RGB(255, 102, 117)
#define GREEN_BG LCD_RGB(18, 53, 49)
#define WARN_BG LCD_RGB(58, 43, 27)
#define RED_BG LCD_RGB(59, 25, 36)

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
} frame;

static void text(int x, int y, const char *s, uint16_t color, uint16_t bg) {
  BSP_LCD_Text(x, y, s, 16U, 1U, color, bg);
}

static void title(int x, int y, const char *s, uint16_t color, uint16_t bg) {
  BSP_LCD_Text(x, y, s, 20U, 1U, color, bg);
}

static void center(int y, const char *s, uint8_t size, uint8_t scale,
                   uint16_t color, uint16_t bg) {
  int x = (240 - BSP_LCD_TextWidth(s, size, scale)) / 2;
  BSP_LCD_Text(x, y, s, size, scale, color, bg);
}

static uint16_t signal_bar_color(unsigned bar) {
  return frame.gps.signal_valid && frame.gps.signal_level >= bar ? CYAN : EDGE;
}

static void signal_bars(int x, int bottom, int width, int gap, int min_height,
                        int step, int radius) {
  for (unsigned i = 0; i < UI_SIGNAL_BARS; ++i) {
    int height = min_height + (int)i * step;
    BSP_LCD_RoundRect(x + (int)i * (width + gap), bottom - height, width,
                      height, radius, signal_bar_color(i));
  }
}

static void header(const char *s) {
  title(12, 10, s, WHITE, BG);
  /* Equal 20px cells, 12px gaps; every icon is centered at local (10,10). */
  const int x = 144, y = 10, pitch = 32;
  BSP_LCD_Icon(x, y, ui_wifi_icon, UI_STATUS_ICON_SIZE,
               frame.esp.wifi_connected ? CYAN : EDGE, BG);
  BSP_LCD_Icon(x + pitch, y, ui_mqtt_icon, UI_STATUS_ICON_SIZE,
               frame.online ? GREEN : EDGE, BG);
  for (unsigned i = 0; i < UI_SIGNAL_BARS; ++i)
    BSP_LCD_Icon(x + 2 * pitch, y, ui_signal_icons[i], UI_STATUS_ICON_SIZE,
                 signal_bar_color(i), BG);
  BSP_LCD_Rect(12, 36, 216, 1, EDGE);
}

static void footer(void) {
  const char *right_hint = "K2 测试";
  BSP_LCD_Rect(12, 216, 216, 1, EDGE);
  text(12, 222, "K1 翻页", MUTED, BG);
  text(228 - BSP_LCD_TextWidth(right_hint, 16U, 1U), 222, right_hint, MUTED,
       BG);
  for (unsigned i = 0; i < UI_PAGE_COUNT; ++i)
    BSP_LCD_RoundRect(94 + (int)i * 10, 228, frame.page == i ? 7 : 4, 4, 2,
                      frame.page == i ? CYAN : EDGE);
}

static const char *gps_status(void) {
  return frame.gps.fix ? "定位成功"
                       : (frame.gps.has_position ? "历史位置" : "等待定位");
}

static uint8_t mpu_ready(void) {
  return frame.mpu_state == BSP_MPU6050_READY && frame.mpu.samples != 0U;
}

static const char *fall_status(void) {
  if (frame.mpu_state == BSP_MPU6050_ERROR)
    return "传感故障";
  if (frame.mpu_state == BSP_MPU6050_CALIBRATING)
    return "静置校准";
  if (!mpu_ready())
    return "正在启动";
  if (frame.fall.state == APP_FALL_WAIT_UPRIGHT)
    return "请扶正设备";
  if (frame.fall.state == APP_FALL_HOLDING)
    return "注意倾斜";
  return "监测正常";
}

static void draw_home(void) {
  char s[40];
  uint8_t safe = mpu_ready() && frame.fall.state == APP_FALL_IDLE;
  uint16_t accent =
      safe ? GREEN : (frame.mpu_state == BSP_MPU6050_ERROR ? RED : AMBER);
  uint16_t card =
      safe ? GREEN_BG
           : (frame.mpu_state == BSP_MPU6050_ERROR ? RED_BG : WARN_BG);
  header("跌倒守护");
  if (frame.rtc.valid)
    snprintf(s, sizeof(s), "%04u / %02u / %02u", (unsigned)frame.rtc.local.year,
             (unsigned)frame.rtc.local.month, (unsigned)frame.rtc.local.day);
  else
    snprintf(s, sizeof(s), "等待时间同步");
  text(14, 45, s, MUTED, BG);
  if (frame.rtc.valid)
    snprintf(s, sizeof(s), "%02u:%02u", (unsigned)frame.rtc.local.hour,
             (unsigned)frame.rtc.local.minute);
  else
    snprintf(s, sizeof(s), "--:--");
  BSP_LCD_Clock(10, 65, s, WHITE, BG);
  if (frame.rtc.valid)
    snprintf(s, sizeof(s), "%02u", (unsigned)frame.rtc.local.second);
  else
    snprintf(s, sizeof(s), "--");
  title(185, 89, s, CYAN, BG);
  snprintf(s, sizeof(s), "UTC%+d", APP_TIMEZONE_HOURS);
  text(180, 66, s, MUTED, BG);
  BSP_LCD_RoundRect(12, 125, 216, 55, 12, card);
  title(24, 132, fall_status(), accent, card);
  text(24, 158, safe ? "持续监测中" : "请检查设备状态", MUTED, card);
  if (safe) {
    BSP_LCD_Circle(202, 150, 12, GREEN);
    BSP_LCD_Line(196, 150, 200, 154, GREEN_BG);
    BSP_LCD_Line(200, 154, 208, 145, GREEN_BG);
  }
  text(14, 192, gps_status(), frame.gps.fix ? CYAN : MUTED, BG);
  text(146, 192, frame.online ? "云端在线" : "云端离线",
       frame.online ? GREEN : AMBER, BG);
}

static void coordinate(char *s, size_t n, int32_t value, uint8_t valid,
                       uint8_t lon) {
  long v = (long)value, m = v < 0 ? -v : v;
  if (!valid)
    snprintf(s, n, "--.------");
  else
    snprintf(s, n, "%ld.%06ld %c", m / 1000000L, m % 1000000L,
             lon ? (v < 0 ? 'W' : 'E') : (v < 0 ? 'S' : 'N'));
}

static void draw_position(void) {
  char s[40];
  header("位置坐标");
  for (unsigned i = 0; i < 2U; ++i) {
    int y = 46 + (int)i * 69;
    BSP_LCD_RoundRect(12, y, 216, 61, 10, CARD);
    text(24, y + 7, i ? "经度" : "纬度", MUTED, CARD);
    coordinate(s, sizeof(s),
               i ? frame.gps.longitude_udeg : frame.gps.latitude_udeg,
               frame.gps.has_position, i);
    title(24, y + 32, s, CYAN, CARD);
  }
  text(14, 190, gps_status(), frame.gps.fix ? GREEN : AMBER, BG);
  if (frame.gps.has_position) {
    uint32_t age = frame.gps.age_ms / 1000U;
    snprintf(s, sizeof(s), "%lus 前",
             (unsigned long)(age > 99999U ? 99999U : age));
    text(143, 190, s, MUTED, BG);
  }
}

static void draw_gps(void) {
  char s[48];
  const bsp_gps_data_t *g = &frame.gps;
  header("卫星信号");
  BSP_LCD_RoundRect(12, 46, 216, 54, 10, CARD);
  text(24, 52, gps_status(), g->fix ? GREEN : AMBER, CARD);
  if (g->signal_valid)
    snprintf(s, sizeof(s), "C/N0  %u dBHz", (unsigned)g->signal_max_dbhz);
  else
    snprintf(s, sizeof(s), "C/N0  --");
  text(24, 77, s, WHITE, CARD);
  signal_bars(171, 93, 7, 4, 8, 9, 2);
  snprintf(s, sizeof(s), "卫星  %u    可见  %u", (unsigned)g->satellites,
           (unsigned)g->satellites_in_view);
  text(14, 111, s, WHITE, BG);
  if (g->has_position) {
    long value = g->altitude_cm, m = value < 0 ? -value : value;
    snprintf(s, sizeof(s), "海拔  %c%ld.%02ld m", value < 0 ? '-' : '+',
             m / 100L, m % 100L);
  } else
    snprintf(s, sizeof(s), "海拔  --");
  text(14, 137, s, WHITE, BG);
  if (g->hdop_valid)
    snprintf(s, sizeof(s), "精度  HDOP %lu.%02lu",
             (unsigned long)(g->hdop_x100 / 100U),
             (unsigned long)(g->hdop_x100 % 100U));
  else
    snprintf(s, sizeof(s), "精度  HDOP --");
  text(14, 163, s, MUTED, BG);
  if (g->utc_valid)
    snprintf(s, sizeof(s), "UTC %02u:%02u:%02u  %02u/%02u",
             (unsigned)g->utc_hour, (unsigned)g->utc_minute,
             (unsigned)g->utc_second, (unsigned)g->utc_month,
             (unsigned)g->utc_day);
  else
    snprintf(s, sizeof(s), "UTC 等待时间数据");
  text(14, 190, s, MUTED, BG);
}

static void signed_value(char *s, size_t n, int32_t value, long divisor) {
  long v = value, m = v < 0 ? -v : v;
  snprintf(s, n, "%c%ld.%02ld", v < 0 ? '-' : '+', m / divisor,
           (m % divisor) * 100L / divisor);
}

static void draw_motion(void) {
  char s[40], value[20];
  header("姿态监测");
  if (!mpu_ready()) {
    BSP_LCD_RoundRect(12, 53, 216, 126, 12, CARD);
    center(76, fall_status(), 20U, 1U, AMBER, CARD);
    center(122,
           frame.mpu_state == BSP_MPU6050_ERROR ? "请检查传感器接线"
                                                : "保持静止约三秒",
           16U, 1U, MUTED, CARD);
    return;
  }
  for (unsigned i = 0; i < 2U; ++i) {
    int x = 12 + (int)i * 112;
    uint16_t accent =
        abs(frame.mpu.angle_cdeg[i]) >= APP_FALL_ANGLE_DEG * 100 ? AMBER : CYAN;
    BSP_LCD_RoundRect(x, 46, 104, 63, 10, CARD);
    text(x + 10, 51, i ? "俯仰角" : "横滚角", MUTED, CARD);
    signed_value(s, sizeof(s), frame.mpu.angle_cdeg[i], 100L);
    title(x + 8, 78, s, accent, CARD);
  }
  text(14, 116, "加速度 g", MUTED, BG);
  text(128, 116, "角速度 d/s", MUTED, BG);
  for (unsigned i = 0; i < 3U; ++i) {
    signed_value(value, sizeof(value), frame.mpu.accel_mg[i], 1000L);
    snprintf(s, sizeof(s), "%c %s", 'X' + i, value);
    text(14, 139 + (int)i * 20, s, WHITE, BG);
    signed_value(value, sizeof(value), frame.mpu.gyro_mdps[i], 1000L);
    snprintf(s, sizeof(s), "%c %s", 'X' + i, value);
    text(128, 139 + (int)i * 20, s, WHITE, BG);
  }
  if ((APP_FALL_HOLD_MS % 1000U) == 0U)
    snprintf(s, sizeof(s), "阈值 %u度 / 持续 %lus",
             (unsigned int)APP_FALL_ANGLE_DEG,
             (unsigned long)(APP_FALL_HOLD_MS / 1000U));
  else
    snprintf(s, sizeof(s), "阈值 %u度 / 持续 %lums",
             (unsigned int)APP_FALL_ANGLE_DEG, (unsigned long)APP_FALL_HOLD_MS);
  text(14, 198, s, AMBER, BG);
}

static const char *network_stage(void) {
  if (frame.online)
    return "数据上报已就绪";
  switch (frame.esp.state) {
  case BSP_ESP_SNTP_CONFIG:
  case BSP_ESP_SNTP_QUERY:
    return "正在同步时间";
  case BSP_ESP_NO_CONFIG:
    return "请配置网络参数";
  case BSP_ESP_UNSUPPORTED:
    return "模块固件不支持";
  case BSP_ESP_BACKOFF:
  case BSP_ESP_RESET_LOW:
    return "等待重新连接";
  case BSP_ESP_MQTT_CLEAN:
  case BSP_ESP_MQTT_USER:
  case BSP_ESP_MQTT_CONFIG:
  case BSP_ESP_MQTT_CONNECT:
  case BSP_ESP_MQTT_SUBSCRIBE:
    return "正在连接云端";
  default:
    return "正在连接网络";
  }
}

static void draw_network(void) {
  char s[40];
  header("网络服务");
  BSP_LCD_RoundRect(12, 46, 216, 64, 10, CARD);
  title(24, 54, frame.online ? "云端在线" : "云端离线",
        frame.online ? GREEN : AMBER, CARD);
  text(24, 85, network_stage(), MUTED, CARD);
  text(14, 122, "设备编号", MUTED, BG);
  title(137, 120, APP_DEVICE_ID, WHITE, BG);
  text(14, 148, "上报周期", MUTED, BG);
  snprintf(s, sizeof(s), "%lu 秒",
           (unsigned long)frame.report.interval_seconds);
  text(137, 148, s, WHITE, BG);
  text(14, 174, "发送成功", MUTED, BG);
  snprintf(s, sizeof(s), "%lu", (unsigned long)frame.esp.published);
  text(137, 174, s, CYAN, BG);
  text(14, 199, frame.esp.wifi_connected ? "无线已连接" : "无线未连接", MUTED,
       BG);
}

static void draw_alarm(void) {
  uint8_t automatic = frame.fall.source == APP_FALL_SOURCE_AUTO;
  BSP_LCD_RoundRect(12, 12, 216, 28, 8, RED_BG);
  center(17, automatic ? "检测到跌倒" : "手动测试报警", 16U, 1U, RED, RED_BG);
  for (int i = 0; i < 3; ++i) {
    BSP_LCD_Line(120, 50 + i, 96 + i, 89, RED);
    BSP_LCD_Line(120, 50 + i, 144 - i, 89, RED);
    BSP_LCD_Line(96, 89 + i, 144, 89 + i, RED);
  }
  BSP_LCD_RoundRect(118, 64, 4, 13, 1, WHITE);
  BSP_LCD_Circle(120, 83, 2, WHITE);
  center(101, automatic ? "跌倒报警" : "测试报警", 40U, 1U, WHITE, BG);
  center(
      150,
      frame.audio == APP_AUDIO_PLAYING
          ? "语音求助播放中"
          : (frame.audio == APP_AUDIO_ERROR ? "语音故障请检查" : "语音未播放"),
      16U, 1U, MUTED, BG);
  text(18, 177, frame.online ? "云端在线" : "云端离线",
       frame.online ? GREEN : AMBER, BG);
  text(145, 177, gps_status(), frame.gps.fix ? CYAN : AMBER, BG);
  BSP_LCD_RoundRect(12, 205, 216, 29, 9, RED);
  center(210, "按 K2 取消报警", 16U, 1U, BG, RED);
}

static void snapshot(void) {
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
}

void app_ui_init(void) {
  page = alarm_active = rendering = 0U;
  dirty = 1U;
  next_y = 0U;
  last_refresh = HAL_GetTick();
  last_lcd_state = ST7789_WAIT_POWER;
  ST7789_Init();
}

void app_ui_next_page(void) {
  if (alarm_active)
    return;
  page = (uint8_t)((page + 1U) % UI_PAGE_COUNT);
  dirty = 1U;
}

void app_ui_set_alarm(uint8_t active) {
  if (alarm_active == active)
    return;
  alarm_active = active;
  if (!active)
    page = 0U;
  dirty = 1U;
}

void app_ui_poll(void) {
  uint32_t now = HAL_GetTick();
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
  if (frame.alarm)
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
