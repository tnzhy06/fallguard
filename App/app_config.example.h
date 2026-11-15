#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/*
 * Fallguard 统一配置。复制为 App/app_config.h 后修改，CMake
 * 首次配置会自动复制。 app_config.h 包含连接凭据并由 Git
 * 忽略，本模板保存可共享的默认值。
 * 时间单位在名称中标明，缓冲容量单位为字节，队列容量单位为条。
 * 引脚、MCU 时钟及外设波特率在 fallguard.ioc 中配置，PCM 属性由转换工具生成。
 */

/* 设备身份。修改编号会同时更新 JSON id、MQTT client ID 和主题。 */
#define APP_DEVICE_ID "belt01"
#define APP_MQTT_TOPIC_PREFIX "fallguard"
#define BSP_ESP_TOPIC_LOC APP_MQTT_TOPIC_PREFIX "/" APP_DEVICE_ID "/loc"
#define BSP_ESP_TOPIC_ALARM APP_MQTT_TOPIC_PREFIX "/" APP_DEVICE_ID "/alarm"
#define BSP_ESP_TOPIC_CMD APP_MQTT_TOPIC_PREFIX "/" APP_DEVICE_ID "/cmd"

/* 网络模块。选择后重新编译烧录，两种模块共用下方连接参数。 */
#define ESP_MODULE_ESP01S 1U
#define ESP_MODULE_AI_WB2_01S 2U
#define ESP_MODULE_TYPE ESP_MODULE_ESP01S

/* Wi-Fi 与 MQTT 连接。空 SSID / HOST 表示尚未配置。 */
#define ESP_WIFI_SSID ""
#define ESP_WIFI_PASSWORD ""
#define MQTT_HOST ""
#define MQTT_PORT 1883U
#define MQTT_USERNAME ""
#define MQTT_PASSWORD ""
#define MQTT_KEEPALIVE_SECONDS 120U
/* Combo 固件的 TCP 心跳间隔及订阅状态查询间隔。 */
#define MQTT_TCP_KEEPALIVE_SECONDS 10U
#define ESP_MQTT_QUERY_INTERVAL_MS 250U

/* 课堂手持演示：回正布防 -> 快速倾倒 -> 保持倾倒姿态。角度为度，时间为 ms。 */
#define APP_FALL_ANGLE_DEG 60
#define APP_FALL_HOLD_MS 600U
/* 离开回正区后，必须在此窗口内达到倾倒角度；慢慢摆放不会报警。 */
#define APP_FALL_TILT_WINDOW_MS 300U
#define APP_FALL_UPRIGHT_ANGLE_DEG 35
#define APP_FALL_UPRIGHT_HOLD_MS 300U
/* 更长的样本间隔清除候选与布防计时，恢复后需重新回正。 */
#define APP_FALL_SAMPLE_GAP_MS 100U

/* MPU6050。INT 接 PA1 / EXTI1；AD0 接地使用 0x68，接高电平使用 0x69。
 * 校准数量按有效 DATA_RDY 中断样本计数，超时用于检测 INT 缺失或采样停止。 */
#define MPU6050_I2C_ADDRESS 0x68U
#define MPU6050_TIMEOUT_MS 5U
/* Deadline for an asynchronous sample read or waiting for an idle bus. */
#define BSP_MPU6050_READ_TIMEOUT_MS 5U
#define MPU6050_SAMPLE_RATE_DIV 9U
#define MPU6050_DLPF_CFG 3U
#define MPU6050_KALMAN_Q_ANGLE 0.001
#define MPU6050_KALMAN_Q_BIAS 0.003
#define MPU6050_KALMAN_R_MEASURE 0.03
#define BSP_MPU6050_START_DELAY_MS 100U
#define BSP_MPU6050_SETTLE_MS 250U
#define BSP_MPU6050_CALIBRATION_SAMPLES 200U
#define BSP_MPU6050_RETRY_MS 2000U
#define BSP_MPU6050_SAMPLE_TIMEOUT_MS 250U
#define BSP_MPU6050_BUS_EDGE_MS 1U
#define BSP_MPU6050_SCL_TIMEOUT_MS 20U

/* 报警语音。完整语音之后插入静音，0 表示连续循环。 */
#define APP_AUDIO_REPEAT_GAP_MS 1000U
#define AUDIO_HALF_FRAMES 256U

/* 主循环调度、按键与 LED。LED 参数表示电平翻转间隔。 */
#define APP_KEY_SCAN_MS 5U
#define KEY_DEBOUNCE_MS 20U
#define APP_UART_RX_RETRY_MS 5U
#define LED_NORMAL_INTERVAL_MS 500U
#define LED_ALARM_INTERVAL_MS 100U

/* 定位。信号等级阈值为 C/N0，单位 dB-Hz。 */
#define GPS_RING_SIZE 1024U
#define GPS_FRAME_SIZE 128U
#define GPS_STALE_MS 5000U
#define GPS_POLL_BUDGET 128U
#define GPS_SIGNAL_MEDIUM_DBHZ 30U
#define GPS_SIGNAL_STRONG_DBHZ 40U

/* MQTT 上报。远程修改周期在重启后恢复为默认值。 */
#define APP_REPORT_DEFAULT_SECONDS 5U
#define APP_REPORT_MIN_SECONDS 1U
#define APP_REPORT_MAX_SECONDS 3600U
#define APP_REPORT_EVENT_QUEUE_SIZE 4U
#define APP_REPORT_COMMAND_BUDGET 4U
#define BSP_ESP_PAYLOAD_SIZE 128U

/* ESP 串口解析和重连。AT 发送缓冲遵循协议的 256 字节限制。 */
#define ESP_RING_SIZE 1024U
#define ESP_LINE_SIZE 192U
#define ESP_COMMAND_QUEUE_SIZE 4U
#define ESP_POLL_BUDGET 128U
#define ESP_BOOT_MS 2000U
#define ESP_RECONNECT_MS 30000U
#define ESP_RESET_LOW_MS 100U
#define ESP_AT_TIMEOUT_MS 2000U
#define ESP_JOIN_TIMEOUT_MS 20000U
#define ESP_MQTT_CONNECT_TIMEOUT_MS 30000U
#define ESP_MQTT_TIMEOUT_MS 10000U
#define ESP_PAYLOAD_TIMEOUT_MS 2000U

/* SNTP 与 RTC。时区采用整数小时，范围 -12 到 +14。 */
#define APP_TIMEZONE_HOURS 8
#define APP_RTC_REFRESH_MS 200U
#define ESP_SNTP_SERVER_1 "ntp.aliyun.com"
#define ESP_SNTP_SERVER_2 "ntp.sjtu.edu.cn"
#define ESP_SNTP_SERVER_3 "cn.ntp.org.cn"
#define ESP_SNTP_QUERY_INTERVAL_MS 2000U
#define ESP_SNTP_WINDOW_MS 30000U
#define ESP_SNTP_RETRY_MS 60000U
#define ESP_RESYNC_MS 3600000U

/* ST7789 240x240 屏幕。背光有效电平 1 为高，0 为低。 */
#define ST7789_WIDTH 240U
#define ST7789_HEIGHT 240U
#define ST7789_ROTATION 2U
#define ST7789_BGR 0U
#define ST7789_INVERSION 1U
#define ST7789_USE_CS 1U
#define ST7789_BL_ACTIVE 1U
#define LCD_BAND_ROWS 16U
#define APP_UI_REFRESH_MS 200U
/* Long fields page at complete character boundaries after fitting the font. */
#define APP_UI_TEXT_PAGE_MS 2000U
/* Visible boot log, bounded display wait, sensor deadline and final hold. */
#define APP_BOOT_LOG_STEP_MS 180U
#define APP_BOOT_DISPLAY_TIMEOUT_MS 1800U
#define APP_BOOT_SENSOR_TIMEOUT_MS 8000U
#define APP_BOOT_COMPLETE_HOLD_MS 700U
#define LCD_COMMAND_TIMEOUT_MS 2U
#define LCD_TRANSFER_TIMEOUT_MS 30U
#define LCD_RETRY_MS 2000U
#define LCD_POWER_DELAY_MS 150U
#define LCD_RESET_LOW_MS 20U
#define LCD_RESET_HIGH_MS 120U
#define LCD_SLEEP_OUT_MS 120U

/* 调试日志总开关、分类开关和异步发送参数。 */
#define DEBUG_OUTPUT_ENABLE 1
#define DEBUG_TX_BUFFER_SIZE 2048U
#define DEBUG_TX_CHUNK_SIZE 64U
#define DEBUG_TX_TIMEOUT_MS 100U
#define DEBUG_LOG_BOOT 1
#define DEBUG_LOG_STATUS 1
#define DEBUG_LOG_KEYS 1
#define DEBUG_LOG_ERRORS 1
#define DEBUG_LOG_FALL 1
#define DEBUG_LOG_AUDIO 1
#define DEBUG_LOG_ESP 1
#define DEBUG_LOG_ESP_RX 1
#define DEBUG_LOG_MQTT 1
#define DEBUG_LOG_REPORT 1
#define DEBUG_LOG_TIME 1
#define DEBUG_LOG_TIME_PERIODIC 0
#define DEBUG_TIME_PERIOD_MS 1000U
#define DEBUG_LOG_MPU_ACCEL 0
#define DEBUG_LOG_MPU_GYRO 0
#define DEBUG_LOG_MPU_ANGLES 0
#define DEBUG_MPU_PERIOD_MS 200U
#define DEBUG_MPU_LINE_INTERVAL_MS 20U
#define DEBUG_LOG_GPS_FIX 0
#define DEBUG_LOG_GPS_POSITION 0
#define DEBUG_LOG_GPS_UTC 0
#define DEBUG_LOG_GPS_ALTITUDE 0
#define DEBUG_LOG_GPS_STATS 0
#define DEBUG_LOG_GPS_SIGNAL 0
#define DEBUG_GPS_PERIOD_MS 250U

/* 编译期校验，防止除零、索引溢出和无效外设参数。 */
#if ESP_MODULE_TYPE != ESP_MODULE_ESP01S && ESP_MODULE_TYPE != ESP_MODULE_AI_WB2_01S
#error "Select ESP_MODULE_ESP01S or ESP_MODULE_AI_WB2_01S"
#endif
#if MQTT_TCP_KEEPALIVE_SECONDS < 1U || MQTT_TCP_KEEPALIVE_SECONDS > 65535U || \
    ESP_MQTT_QUERY_INTERVAL_MS < 1U
#error "Invalid Combo keepalive or subscription query interval"
#endif
#if APP_FALL_ANGLE_DEG < 1 || APP_FALL_ANGLE_DEG > 180 || APP_FALL_HOLD_MS < 1U
#error "Invalid fall detection threshold or hold time"
#endif
#if APP_FALL_UPRIGHT_ANGLE_DEG < 0 || APP_FALL_UPRIGHT_ANGLE_DEG >= APP_FALL_ANGLE_DEG
#error "Upright angle must be nonnegative and below the fall angle"
#endif
#if APP_FALL_TILT_WINDOW_MS < 1U || APP_FALL_UPRIGHT_HOLD_MS < 1U || \
    APP_FALL_SAMPLE_GAP_MS < 1U || APP_FALL_SAMPLE_GAP_MS > BSP_MPU6050_SAMPLE_TIMEOUT_MS
#error "Invalid demo gesture timing or sample gap"
#endif
#if MPU6050_I2C_ADDRESS != 0x68U && MPU6050_I2C_ADDRESS != 0x69U
#error "MPU6050 I2C address must be 0x68 or 0x69"
#endif
#if MPU6050_SAMPLE_RATE_DIV > 255U || MPU6050_DLPF_CFG > 6U
#error "Invalid MPU6050 sample rate divider or DLPF setting"
#endif
#if BSP_MPU6050_CALIBRATION_SAMPLES < 1U ||                                    \
    BSP_MPU6050_CALIBRATION_SAMPLES > 65535U
#error "MPU6050 calibration samples must fit uint16_t and be nonzero"
#endif
#if BSP_MPU6050_BUS_EDGE_MS < 1U || BSP_MPU6050_SAMPLE_TIMEOUT_MS < 1U ||      \
    BSP_MPU6050_SCL_TIMEOUT_MS < BSP_MPU6050_BUS_EDGE_MS
#error "Invalid MPU6050 interrupt or recovery timing"
#endif
#if BSP_MPU6050_READ_TIMEOUT_MS < 1U || \
    BSP_MPU6050_READ_TIMEOUT_MS > BSP_MPU6050_SAMPLE_TIMEOUT_MS
#error "Invalid MPU6050 asynchronous read timeout"
#endif
#if AUDIO_HALF_FRAMES < 1U || AUDIO_HALF_FRAMES > 16383U ||                    \
    APP_AUDIO_REPEAT_GAP_MS > 268435455U
#error "Audio DMA buffer or silent interval is too large"
#endif
#if APP_KEY_SCAN_MS < 1U || KEY_DEBOUNCE_MS < 1U ||                            \
    APP_UART_RX_RETRY_MS < 1U || APP_UI_REFRESH_MS < 1U
#error "Polling periods must be nonzero"
#endif
#if APP_REPORT_MIN_SECONDS < 1U || APP_REPORT_MAX_SECONDS > 4294967U ||        \
    APP_REPORT_MIN_SECONDS > APP_REPORT_MAX_SECONDS ||                         \
    APP_REPORT_DEFAULT_SECONDS < APP_REPORT_MIN_SECONDS ||                     \
    APP_REPORT_DEFAULT_SECONDS > APP_REPORT_MAX_SECONDS
#error "Invalid MQTT reporting interval range"
#endif
#if APP_REPORT_EVENT_QUEUE_SIZE < 1U || APP_REPORT_EVENT_QUEUE_SIZE > 255U ||  \
    ESP_COMMAND_QUEUE_SIZE < 1U || ESP_COMMAND_QUEUE_SIZE > 255U ||            \
    APP_REPORT_COMMAND_BUDGET < 1U || APP_REPORT_COMMAND_BUDGET > 255U
#error "Queue sizes and command budget must fit uint8_t and be nonzero"
#endif
#if GPS_RING_SIZE < 2U || GPS_RING_SIZE > 65535U || ESP_RING_SIZE < 2U ||      \
    ESP_RING_SIZE > 65535U || GPS_FRAME_SIZE < 12U ||                          \
    GPS_FRAME_SIZE > 65535U || ESP_LINE_SIZE < 32U ||                          \
    ESP_LINE_SIZE > 65535U || BSP_ESP_PAYLOAD_SIZE < 2U ||                     \
    BSP_ESP_PAYLOAD_SIZE > 65535U
#error "Invalid UART receive buffer size"
#endif
#if GPS_POLL_BUDGET < 1U || ESP_POLL_BUDGET < 1U ||                            \
    ESP_POLL_BUDGET > 65535U || GPS_SIGNAL_MEDIUM_DBHZ < 1U ||                 \
    GPS_SIGNAL_STRONG_DBHZ <= GPS_SIGNAL_MEDIUM_DBHZ ||                        \
    GPS_SIGNAL_STRONG_DBHZ > 99U
#error "Invalid UART poll budget or GPS signal thresholds"
#endif
#if MQTT_PORT < 1U || MQTT_PORT > 65535U || MQTT_KEEPALIVE_SECONDS > 7200U
#error "Invalid MQTT port or keepalive"
#endif
#if APP_TIMEZONE_HOURS < -12 || APP_TIMEZONE_HOURS > 14 ||                     \
    APP_RTC_REFRESH_MS < 1U
#error "Invalid RTC timezone or refresh period"
#endif
#if ST7789_WIDTH != 240U || ST7789_HEIGHT != 240U
#error "The current UI requires a 240x240 panel"
#endif
#if ST7789_ROTATION > 3U || ST7789_BGR > 1U || ST7789_INVERSION > 1U ||        \
    ST7789_USE_CS > 1U || ST7789_BL_ACTIVE > 1U
#error "Invalid ST7789 panel option"
#endif
#if LCD_BAND_ROWS < 1U
#error "LCD band rows must be nonzero"
#elif (ST7789_HEIGHT % LCD_BAND_ROWS) != 0U ||                                 \
    (ST7789_WIDTH * LCD_BAND_ROWS * 2U) > 65535U
#error "LCD strips must tile the panel and fit the HAL DMA transfer length"
#endif
#if APP_UI_TEXT_PAGE_MS < 1U
#error "Text page interval must be nonzero"
#endif
#if APP_BOOT_LOG_STEP_MS < 1U || APP_BOOT_DISPLAY_TIMEOUT_MS < APP_BOOT_LOG_STEP_MS || \
    APP_BOOT_SENSOR_TIMEOUT_MS < 1U || APP_BOOT_COMPLETE_HOLD_MS < 1U
#error "Invalid boot display timing"
#endif
#if DEBUG_TX_BUFFER_SIZE < 2U || DEBUG_TX_BUFFER_SIZE > 65535U ||              \
    DEBUG_TX_CHUNK_SIZE < 1U || DEBUG_TX_CHUNK_SIZE >= DEBUG_TX_BUFFER_SIZE || \
    DEBUG_TX_TIMEOUT_MS < 1U
#error "Invalid nonblocking log buffer or timeout"
#endif

#endif
