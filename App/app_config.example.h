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
#define APP_DEVICE_ID "belt01" /* 设备唯一编号，同时作为 MQTT Client ID 和 JSON id。 */
#define APP_MQTT_TOPIC_PREFIX "fallguard" /* 本项目所有 MQTT 主题共用的根前缀。 */
#define BSP_ESP_TOPIC_LOC APP_MQTT_TOPIC_PREFIX "/" APP_DEVICE_ID "/loc" /* 周期和按需位置上报主题。 */
#define BSP_ESP_TOPIC_ALARM APP_MQTT_TOPIC_PREFIX "/" APP_DEVICE_ID "/alarm" /* 跌倒及解除事件上报主题。 */
#define BSP_ESP_TOPIC_CMD APP_MQTT_TOPIC_PREFIX "/" APP_DEVICE_ID "/cmd" /* 云端下发控制命令的订阅主题。 */

/* Wi-Fi 与 MQTT 连接。空 SSID / HOST 表示尚未配置。 */
#define ESP_WIFI_SSID "" /* ESP-01S 要连接的 2.4GHz Wi-Fi 名称，留空禁用联网。 */
#define ESP_WIFI_PASSWORD "" /* Wi-Fi 密码；开放网络可填写空字符串。 */
#define MQTT_HOST "" /* MQTT Broker 的域名或 IPv4 地址，留空禁用 MQTT。 */
#define MQTT_PORT 1883U /* MQTT Broker TCP 端口，普通非 TLS 连接通常为 1883。 */
#define MQTT_USERNAME "" /* MQTT 登录用户名，Broker 不鉴权时可留空。 */
#define MQTT_PASSWORD "" /* MQTT 登录密码，Broker 不鉴权时可留空。 */
#define MQTT_KEEPALIVE_SECONDS 120U /* MQTT 心跳保活周期，单位为秒。 */

/* 课堂手持演示：回正布防 -> 快速倾倒 -> 保持倾倒姿态。角度为度，时间为 ms。 */
#define APP_FALL_ANGLE_DEG 60 /* Roll 或 Pitch 达到该绝对角度后进入倾倒保持阶段。 */
#define APP_FALL_HOLD_MS 600U /* 倾倒角度持续达到该时间后触发自动报警。 */
/* 离开回正区后，必须在此窗口内达到倾倒角度；慢慢摆放不会报警。 */
#define APP_FALL_TILT_WINDOW_MS 300U /* 离开回正区后到达倾倒角度的最长允许时间。 */
#define APP_FALL_UPRIGHT_ANGLE_DEG 35 /* Roll 和 Pitch 均不超过该角度时视为回正。 */
#define APP_FALL_UPRIGHT_HOLD_MS 300U /* 连续回正达到该时间后完成布防。 */
/* 更长的样本间隔清除候选与布防计时，恢复后需重新回正。 */
#define APP_FALL_SAMPLE_GAP_MS 100U /* 相邻有效样本超过该间隔时放弃本次判断并重新布防。 */

/* MPU6050。INT 接 PA1 / EXTI1；AD0 接地使用 0x68，接高电平使用 0x69。
 * 校准数量按有效 DATA_RDY 中断样本计数，超时用于检测 INT 缺失或采样停止。 */
#define MPU6050_I2C_ADDRESS 0x68U /* MPU6050 的 7 位 I2C 地址，由 AD0 电平决定。 */
#define MPU6050_TIMEOUT_MS 5U /* 启动阶段寄存器轮询读写的 HAL 超时。 */
#define BSP_MPU6050_READ_TIMEOUT_MS 5U /* 异步采样事务或等待总线空闲的截止时间。 */
#define MPU6050_SAMPLE_RATE_DIV 9U /* 采样分频值；DLPF 开启时 1kHz/(1+9)=100Hz。 */
#define MPU6050_DLPF_CFG 3U /* 数字低通滤波配置，3 对应约 42/44Hz 带宽。 */
#define MPU6050_KALMAN_Q_ANGLE 0.001 /* Kalman 角度过程噪声协方差。 */
#define MPU6050_KALMAN_Q_BIAS 0.003 /* Kalman 陀螺仪偏置过程噪声协方差。 */
#define MPU6050_KALMAN_R_MEASURE 0.03 /* Kalman 加速度角度测量噪声协方差。 */
#define BSP_MPU6050_START_DELAY_MS 100U /* 复位和各启动步骤之间的等待时间。 */
#define BSP_MPU6050_SETTLE_MS 250U /* 寄存器配置完成后等待传感器稳定的时间。 */
#define BSP_MPU6050_CALIBRATION_SAMPLES 200U /* 上电陀螺仪零偏校准所需有效样本数。 */
#define BSP_MPU6050_RETRY_MS 2000U /* 传感器故障后再次恢复 I2C 总线的等待时间。 */
#define BSP_MPU6050_SAMPLE_TIMEOUT_MS 250U /* 未收到有效 DATA_RDY 样本时判定故障的时间。 */
#define BSP_MPU6050_BUS_EDGE_MS 1U /* I2C 总线恢复时每个 SCL/SDA 电平阶段的保持时间。 */
#define BSP_MPU6050_SCL_TIMEOUT_MS 20U /* 总线恢复时等待 SCL 被释放为高电平的最长时间。 */

/* 报警语音。完整语音之后插入静音，0 表示连续循环。 */
#define APP_AUDIO_REPEAT_GAP_MS 1000U /* 每遍报警语音播放完成后的静音间隔。 */
#define AUDIO_HALF_FRAMES 256U /* I2S 循环 DMA 每半缓冲包含的单声道采样帧数。 */

/* 主循环调度、按键与 LED。LED 参数表示电平翻转间隔。 */
#define APP_KEY_SCAN_MS 5U /* 主循环扫描 K1/K2 的时间间隔。 */
#define KEY_DEBOUNCE_MS 20U /* 按键电平稳定达到该时间后确认状态改变。 */
#define APP_UART_RX_RETRY_MS 5U /* GPS/ESP 中断接收启动失败后的重试间隔。 */
#define LED_NORMAL_INTERVAL_MS 500U /* 正常在线状态 LED 翻转间隔。 */
#define LED_ALARM_INTERVAL_MS 100U /* 报警状态 LED 快闪翻转间隔。 */

/* 定位。信号等级阈值为 C/N0，单位 dB-Hz。 */
#define GPS_RING_SIZE 1024U /* USART2 GPS 接收环形缓冲区容量，单位为字节。 */
#define GPS_FRAME_SIZE 128U /* 单条 NMEA 语句缓冲区容量，包含结尾和终止符。 */
#define GPS_STALE_MS 5000U /* 定位或卫星信号超过该时间未更新即标记为过期。 */
#define GPS_POLL_BUDGET 128U /* 主循环单次最多处理的 GPS 接收字节数。 */
#define GPS_SIGNAL_MEDIUM_DBHZ 30U /* 中等卫星信号等级的最低 C/N0。 */
#define GPS_SIGNAL_STRONG_DBHZ 40U /* 强卫星信号等级的最低 C/N0。 */

/* MQTT 上报。远程修改周期在重启后恢复为默认值。 */
#define APP_REPORT_DEFAULT_SECONDS 5U /* 上电后的默认位置上报周期。 */
#define APP_REPORT_MIN_SECONDS 1U /* 远程命令允许设置的最短上报周期。 */
#define APP_REPORT_MAX_SECONDS 3600U /* 远程命令允许设置的最长上报周期。 */
#define APP_REPORT_EVENT_QUEUE_SIZE 4U /* 在线期间待发送报警事件队列容量。 */
#define APP_REPORT_COMMAND_BUDGET 4U /* 主循环单次最多处理的 MQTT 下行命令数。 */
#define BSP_ESP_PAYLOAD_SIZE 128U /* MQTT 发布和接收载荷缓冲区容量，单位为字节。 */

/* ESP 串口解析和重连。AT 发送缓冲遵循协议的 256 字节限制。 */
#define ESP_RING_SIZE 1024U /* USART3 ESP 接收环形缓冲区容量，单位为字节。 */
#define ESP_LINE_SIZE 192U /* 单行 ESP AT 响应缓冲区容量。 */
#define ESP_COMMAND_QUEUE_SIZE 4U /* 已接收但尚未处理的 MQTT 命令队列容量。 */
#define ESP_POLL_BUDGET 128U /* 主循环单次最多处理的 ESP 接收字节数。 */
#define ESP_BOOT_MS 2000U /* ESP EN 拉高后等待固件启动完成的时间。 */
#define ESP_RECONNECT_MS 30000U /* 网络故障后执行硬件复位前的退避时间。 */
#define ESP_RESET_LOW_MS 100U /* ESP EN 保持低电平复位的时间。 */
#define ESP_AT_TIMEOUT_MS 2000U /* 普通 AT 指令等待最终响应的超时。 */
#define ESP_JOIN_TIMEOUT_MS 20000U /* 加入 Wi-Fi 接入点的响应超时。 */
#define ESP_MQTT_CONNECT_TIMEOUT_MS 30000U /* 建立 MQTT 连接和等待事件的超时。 */
#define ESP_MQTT_TIMEOUT_MS 10000U /* 其他 MQTT 配置、订阅和发布指令超时。 */
#define ESP_PAYLOAD_TIMEOUT_MS 2000U /* 接收 MQTT 载荷中途停顿的超时。 */

/* SNTP 与 RTC。时区采用整数小时，范围 -12 到 +14。 */
#define APP_TIMEZONE_HOURS 8 /* 本地时间相对 UTC 的整数小时偏移。 */
#define APP_RTC_REFRESH_MS 200U /* 将硬件 RTC 秒计数转换为日期时间的刷新周期。 */
#define ESP_SNTP_SERVER_1 "ntp.aliyun.com" /* ESP SNTP 使用的首选时间服务器。 */
#define ESP_SNTP_SERVER_2 "time.cloud.tencent.com" /* 首选服务器不可用时的第二时间服务器。 */
#define ESP_SNTP_SERVER_3 "cn.pool.ntp.org" /* 前两个服务器不可用时的第三时间服务器。 */
#define ESP_SNTP_QUERY_INTERVAL_MS 2000U /* 初次校时窗口内查询 SNTP 时间的间隔。 */
#define ESP_SNTP_WINDOW_MS 30000U /* 一轮 SNTP 初次取时允许持续的最长时间。 */
#define ESP_SNTP_RETRY_MS 60000U /* 一轮 SNTP 未得到有效时间后的再次尝试间隔。 */
#define ESP_RESYNC_MS 3600000U /* 成功校时后重新向网络校时的周期。 */

/* ST7789 240x240 屏幕。背光有效电平 1 为高，0 为低。 */
#define ST7789_WIDTH 240U /* LCD 控制器可见区域宽度，单位为像素。 */
#define ST7789_HEIGHT 240U /* LCD 控制器可见区域高度，单位为像素。 */
#define ST7789_ROTATION 2U /* 屏幕旋转方向，允许值为 0 至 3。 */
#define ST7789_BGR 0U /* 置 1 使用 BGR 颜色顺序，置 0 使用 RGB。 */
#define ST7789_INVERSION 1U /* 置 1 开启面板颜色反显模式。 */
#define ST7789_USE_CS 1U /* 置 1 由驱动控制 LCD 片选引脚。 */
#define ST7789_BL_ACTIVE 1U /* 背光有效电平：1 为高电平，0 为低电平。 */
#define LCD_BAND_ROWS 16U /* 分带渲染时单个 DMA 缓冲包含的像素行数。 */
#define APP_UI_REFRESH_MS 200U /* 正式 UI 生成新画面的最短周期。 */
#define APP_UI_TEXT_PAGE_MS 2000U /* 过长文本按完整字符翻页显示的停留时间。 */
#define APP_BOOT_LOG_STEP_MS 180U /* 启动页相邻两条初始化日志的最短显示间隔。 */
#define APP_BOOT_DISPLAY_TIMEOUT_MS 1800U /* 启动页等待 LCD 完成当前帧的最长时间。 */
#define APP_BOOT_SENSOR_TIMEOUT_MS 8000U /* 启动页等待 MPU6050 校准完成的最长时间。 */
#define APP_BOOT_COMPLETE_HOLD_MS 700U /* 初始化完成提示在进入正式 UI 前的保留时间。 */
#define LCD_COMMAND_TIMEOUT_MS 2U /* LCD 短命令阻塞发送的 HAL 超时。 */
#define LCD_TRANSFER_TIMEOUT_MS 30U /* 单个像素带 SPI DMA 传输的超时。 */
#define LCD_RETRY_MS 2000U /* LCD 传输故障后重新初始化的等待时间。 */
#define LCD_POWER_DELAY_MS 150U /* LCD 驱动启动前等待电源稳定的时间。 */
#define LCD_RESET_LOW_MS 20U /* LCD 硬件复位低电平保持时间。 */
#define LCD_RESET_HIGH_MS 120U /* 释放 LCD 复位后等待控制器就绪的时间。 */
#define LCD_SLEEP_OUT_MS 120U /* 发送退出睡眠命令后等待面板就绪的时间。 */

/* 调试日志总开关、级别、模块、详细跟踪和异步发送参数。 */
#define DEBUG_OUTPUT_ENABLE 1 /* 置 1 启用全部串口日志功能，置 0 编译期关闭日志输出。 */
#define DEBUG_TX_BUFFER_SIZE 2048U /* 非阻塞日志环形缓冲区容量，单位为字节。 */
#define DEBUG_TX_CHUNK_SIZE 64U /* USART1 每次中断发送的最大连续字节数。 */
#define DEBUG_TX_TIMEOUT_MS 100U /* 单次日志发送超过该时间后中止并计为错误。 */
#define DEBUG_LOG_INFO_ENABLE 1 /* 置 1 输出 [I] 普通状态日志。 */
#define DEBUG_LOG_WARN_ENABLE 1 /* 置 1 输出 [W] 可恢复异常和降级日志。 */
#define DEBUG_LOG_ERROR_ENABLE 1 /* 置 1 输出 [E] 错误日志。 */
#define DEBUG_LOG_DEBUG_ENABLE 1 /* 置 1 允许输出 [D] 详细跟踪，仍受下方跟踪开关控制。 */
#define DEBUG_LOG_BOOT_ENABLE 1 /* 启动流程和硬件摘要日志模块开关。 */
#define DEBUG_LOG_RTC_ENABLE 1 /* RTC、SNTP 应用结果和时间状态日志模块开关。 */
#define DEBUG_LOG_TIM_ENABLE 1 /* 定时器启动和故障日志模块开关。 */
#define DEBUG_LOG_MPU_ENABLE 1 /* MPU6050 初始化、恢复、采样和错误日志模块开关。 */
#define DEBUG_LOG_GPS_ENABLE 1 /* GPS 定位和卫星诊断日志模块开关。 */
#define DEBUG_LOG_LCD_ENABLE 1 /* LCD 初始化和 SPI/DMA 故障日志模块开关。 */
#define DEBUG_LOG_KEY_ENABLE 1 /* K1、K2 按键事件日志模块开关。 */
#define DEBUG_LOG_AUDIO_ENABLE 1 /* 报警音频启动、停止和错误日志模块开关。 */
#define DEBUG_LOG_ESP_ENABLE 1 /* ESP-01S、Wi-Fi、AT 和 SNTP 日志模块开关。 */
#define DEBUG_LOG_MQTT_ENABLE 1 /* MQTT 连接、订阅和发布日志模块开关。 */
#define DEBUG_LOG_REPORT_ENABLE 1 /* 应用上报队列和远程命令日志模块开关。 */
#define DEBUG_LOG_FALL_ENABLE 1 /* 跌倒布防、报警和解除日志模块开关。 */
#define DEBUG_TRACE_ESP_AT 1 /* 置 1 输出 ESP AT 指令的 TX/RX 详细内容。 */
#define DEBUG_TRACE_RTC_PERIODIC 0 /* 置 1 周期输出 RTC 完整时间和网络状态。 */
#define DEBUG_TIME_PERIOD_MS 1000U /* RTC 周期跟踪日志的输出间隔。 */
#define DEBUG_TRACE_MPU_ACCEL 0 /* 置 1 周期输出三轴加速度，单位 mg。 */
#define DEBUG_TRACE_MPU_GYRO 0 /* 置 1 周期输出三轴角速度，单位 mdps。 */
#define DEBUG_TRACE_MPU_ANGLES 0 /* 置 1 周期输出 Roll/Pitch 和采样间隔。 */
#define DEBUG_MPU_PERIOD_MS 200U /* 一组 MPU 详细数据的输出周期。 */
#define DEBUG_MPU_LINE_INTERVAL_MS 20U /* 同组 MPU 各日志行之间的最小间隔。 */
#define DEBUG_TRACE_GPS_FIX 0 /* 置 1 输出定位有效性、卫星数和数据年龄。 */
#define DEBUG_TRACE_GPS_POSITION 0 /* 置 1 输出整数微度格式的经纬度。 */
#define DEBUG_TRACE_GPS_UTC 0 /* 置 1 输出 GPS 提供的 UTC 日期时间。 */
#define DEBUG_TRACE_GPS_ALTITUDE 0 /* 置 1 输出海拔和 HDOP。 */
#define DEBUG_TRACE_GPS_STATS 0 /* 置 1 输出 NMEA 计数、校验和及串口错误统计。 */
#define DEBUG_TRACE_GPS_SIGNAL 0 /* 置 1 输出 GSV 可见卫星和 C/N0 信号信息。 */
#define DEBUG_GPS_PERIOD_MS 250U /* 每条 GPS 详细日志之间的最小间隔。 */

/* 编译期校验，防止除零、索引溢出和无效外设参数。 */
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
