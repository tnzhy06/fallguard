# Fallguard

基于 STM32F103RCT6 的腰带式跌倒报警课程演示项目。采用主循环调度，无 RTOS、无 DMA。

- MPU6050：三轴加速度、角速度、Roll/Pitch；任一角度绝对值达到 60° 并持续 1 秒触发报警。
- ATGM336H：GPS 定位、UTC、海拔及信号强度。
- ESP-01S：Wi-Fi、SNTP 校时及 MQTT 上报。
- OLED：主屏、位置、GPS、姿态和网络五页；K1 切页，K2 测试报警/消警。

当前报警通过 OLED、LED 和 MQTT 提示，音频输出待后续接入。

## 构建与调试

使用 VS Code 的 STM32CubeMX/STM32Cube 扩展及其 CMake、Ninja、ARM GCC 工具链。

1. 打开工程目录，使用 `Debug` 预设配置 CMake。
2. 执行默认构建任务 `Fallguard: Build Debug`（Ctrl+Shift+B）。
3. 连接 ST-LINK，选择 `Fallguard: Debug (ST-LINK)`，按 F5 烧录并调试。

固件为 `build/Debug/fallguard.elf`，代码索引使用同目录的 `compile_commands.json`。在 STM32Cube 工具链环境中也可执行：

```sh
cube-cmake --preset Debug
cube-cmake --build --preset Debug
```

配置 Wi-Fi 和 MQTT：

1. 将 `BSP/esp_config.example.h` 复制为 `BSP/esp_config.h`，填写 `ESP_WIFI_SSID` 和 `ESP_WIFI_PASSWORD`。
2. 将 `BSP/mqtt_config.example.h` 复制为 `BSP/mqtt_config.h`，填写 `MQTT_HOST`、`MQTT_PORT`、`MQTT_USERNAME` 和 `MQTT_PASSWORD`。
3. 配置完成后重新编译并烧录。

首次 CMake 配置也会在上述配置头文件不存在时自动从 example 模板创建；已填写的配置不会被覆盖。

## 调试串口

调试日志从 USART1 输出，使用 **3.3 V 电平的 USB 转 TTL 串口模块**连接电脑：

| STM32 引脚 | USB 转 TTL 引脚 | 用途 |
|---|---|---|
| PA9（USART1_TX） | RXD | 接收设备日志 |
| GND | GND | 共地 |
| PA10（USART1_RX） | TXD（可不接） | 当前没有串口交互命令，仅查看日志时不需要 |

打开串口助手，选择对应 COM 口，设置 **115200 波特率、8 位数据位、无校验、1 位停止位、无流控（115200 8N1）**，以文本模式接收。开启相应日志并重新烧录后，先打开串口，再按开发板复位键，即可观察已启用的启动与联网日志。

### 选择输出内容

所有开关集中在 [BSP/debug_config.h](BSP/debug_config.h)，`1` 开启、`0` 关闭。修改后需**重新编译并烧录**；这些是编译配置，不能通过串口发送命令修改。关闭日志不影响采样、GPS 解析、OLED 显示、报警或 MQTT 通信。

当前总开关 `DEBUG_OUTPUT_ENABLE` 和所有 `DEBUG_LOG_*` 分类开关均为 `0`，调试串口不输出日志。需要恢复输出时，将总开关和所需分类同时设为 `1`。

| 宏 | 输出内容 | 默认值 |
|---|---|---|
| `DEBUG_OUTPUT_ENABLE` | 所有调试日志的总开关 | 0 |
| `DEBUG_LOG_BOOT` | 启动横幅、时钟、接线提示 | 0 |
| `DEBUG_LOG_STATUS` | MPU 配置/零偏完成、OLED 初始化成功等状态 | 0 |
| `DEBUG_LOG_KEYS` | 按键操作 | 0 |
| `DEBUG_LOG_ERRORS` | 通用错误提示 | 0 |
| `DEBUG_LOG_FALL` | 报警、消警、扶正重新待命事件 | 0 |
| `DEBUG_LOG_ESP` | AT 发送步骤、联网提示及 ESP 错误；凭据隐藏 | 0 |
| `DEBUG_LOG_ESP_RX` | ESP 返回的响应行；凭据命令回显隐藏 | 0 |
| `DEBUG_LOG_MQTT` | MQTT 连接、订阅、发布和重连状态 | 0 |
| `DEBUG_LOG_REPORT` | 上报 JSON、下行命令处理和上报周期 | 0 |
| `DEBUG_LOG_TIME` | RTC 启动、SNTP 校时及 RTC 写失败事件 | 0 |
| `DEBUG_LOG_TIME_PERIODIC` | 周期时间或未校准状态，需同时开启 `DEBUG_LOG_TIME` | 0 |
| `DEBUG_LOG_MPU_ACCEL` | 三轴加速度，单位 mg（1000 mg = 1 g） | 0 |
| `DEBUG_LOG_MPU_GYRO` | 三轴角速度，单位 mdps（1000 mdps = 1°/s） | 0 |
| `DEBUG_LOG_MPU_ANGLES` | Roll/Pitch，单位 cdeg（100 cdeg = 1°），以及采样间隔 dt/max（ms） | 0 |
| `DEBUG_LOG_GPS_FIX` | 定位有效性、历史位置标志、卫星数、位置年龄 | 0 |
| `DEBUG_LOG_GPS_POSITION` | 经纬度，单位为整数微度（除以 1000000 得到度） | 0 |
| `DEBUG_LOG_GPS_UTC` | GPS UTC 日期时间与有效标志 | 0 |
| `DEBUG_LOG_GPS_ALTITUDE` | 海拔（cm）、HDOP × 100 及有效标志 | 0 |
| `DEBUG_LOG_GPS_STATS` | NMEA 接收、解析、校验及串口错误计数 | 0 |
| `DEBUG_LOG_GPS_SIGNAL` | GSV 可见卫星、最大 C/N₀（dBHz）和信号档位 | 0 |

`DEBUG_LOG_ERRORS=0` 时，ESP 错误仍可随 `DEBUG_LOG_ESP` 输出，RTC 写失败仍可随 `DEBUG_LOG_TIME` 输出。需要完全静默时，将 `DEBUG_OUTPUT_ENABLE` 改为 `0`。

### 常用配置

只看 Roll/Pitch：将其余 `DEBUG_LOG_*` 都设为 `0`，保留以下配置：

```c
#define DEBUG_OUTPUT_ENABLE       1
#define DEBUG_LOG_MPU_ANGLES      1
#define DEBUG_MPU_PERIOD_MS       200U
```

如需同时看六轴数据，再将 `DEBUG_LOG_MPU_ACCEL` 和 `DEBUG_LOG_MPU_GYRO` 改为 `1`。

只看 GPS 位置和信号：将其余 `DEBUG_LOG_*` 都设为 `0`，保留以下配置：

```c
#define DEBUG_OUTPUT_ENABLE       1
#define DEBUG_LOG_GPS_POSITION    1
#define DEBUG_LOG_GPS_SIGNAL      1
#define DEBUG_GPS_PERIOD_MS       250U
```

观察 RTC 连续走时：将 `DEBUG_OUTPUT_ENABLE`、`DEBUG_LOG_TIME` 和 `DEBUG_LOG_TIME_PERIODIC` 同时设为 `1`。RTC 显示为 UTC+8，GPS 日志中的 UTC 保持 UTC。

### 输出周期

| 宏 | 默认值 | 含义 |
|---|---|---|
| `DEBUG_MPU_PERIOD_MS` | `200U` | 开始一组 MPU 日志快照的最小间隔；同组各行使用同一样本号 |
| `DEBUG_GPS_PERIOD_MS` | `250U` | 相邻两条已开启的 GPS 日志行的最小间隔，多项开启时轮流输出 |
| `DEBUG_TIME_PERIOD_MS` | `1000U` | RTC 周期日志间隔，仅在两个 TIME 开关都开启时生效 |

周期单位为 ms，调度可能使实际间隔增加。日志周期不改变 MPU 的 100 Hz 采样、GPS 的 5 秒过期规则或 MQTT 上报周期。

## 使用说明

上电后保持 MPU6050 静止约 3 秒，等待陀螺零偏校准。消警后需扶正至 Roll、Pitch 的绝对值均小于 60°，才会再次自动触发。

默认设备编号 `belt01`，MQTT 主题为 `fallguard/belt01/loc`、`fallguard/belt01/alarm`、`fallguard/belt01/cmd`。默认每 5 秒上报，命令允许设置 1～3600 秒；MQTT 不上报卫星数。调试日志可按上文分类选择。

第三方代码及许可证见 `ThirdParty/`。本项目用于课程演示。
