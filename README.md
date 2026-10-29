# Fallguard

基于 STM32F103RCT6 的腰带式跌倒报警课程演示项目。采用主循环调度，无 RTOS；I2S2 使用 DMA 输出报警语音。

- MPU6050：三轴加速度、角速度、Roll/Pitch；任一角度绝对值达到 60° 并持续 1 秒触发报警。
- ATGM336H：GPS 定位、UTC、海拔及信号强度。
- ESP-01S：Wi-Fi、SNTP 校时及 MQTT 上报。
- MAX98357A：8 kHz、16 位 I2S 语音；自动报警或 K2 测试报警时循环播放，K2 或 MQTT 消警时停止。
- OLED：主屏、位置、GPS、姿态和网络五页；K1 切页，K2 测试报警/消警。

当前报警通过语音、OLED、LED 和 MQTT 提示。OLED 报警页显示 `AUDIO ON` 或 `AUDIO ERROR`。

ESP-01S 的 EN/CH_PD 接 **PB0**（原 PB12 接线需迁移）；TXD 接 PB11，RXD 接 PB10。
I2S2 配置为 8 kHz、16 位、Philips 主机发送，关闭 MCLK，使用 DMA1 Channel 5 循环半字传输。MAX98357A 的信号接线如下：

| STM32 引脚 | I2S2 信号 | MAX98357 输入 |
|---|---|---|
| PB12 | WS | LRC / LRCLK |
| PB13 | CK | BCLK |
| PB15 | SD | DIN |

MAX98357A 的 SD/SD_MODE 保持有效高电平或使用模块板默认使能配置，GAIN 可使用模块默认设置；当前固件不控制这两个引脚。功放与 STM32 共地，供电按模块额定要求连接。扬声器接功放的两个输出端，不将任一输出端接地。

音频来自本地 `docs/播报语音/我摔倒了请帮帮我_8k.wav`，为 8000 Hz、单声道、16 位 PCM，原始时长 2.16 秒。转换时默认只裁掉首尾的全零采样，两端各保留 20 ms 余量（原文件不足时按实际长度保留），所有非零采样和语句中间的停顿均不改变。当前保留原文件第 592～15554 个采样（从 0 开始），共 14963 个单声道采样，时长 1.870375 秒；原 WAV 不修改，报警循环间隔随裁剪缩短。

`App/audio/alarm_pcm.c` 不包含 WAV 文件头，逐样本复制到左右声道，左右声道选择均可播放同一语音。29926 个半字（59852 字节）直接从 Flash 交给 DMA 循环读取，比完整音频的 69120 字节节省 9268 字节（约 13.4%）。不使用整段音频大小的 RAM 缓冲，也不依赖主循环补数。当前系统时钟下实际采样率约 8.007 kHz。

生成的 PCM 源码随工程保存，因此干净检出无需 `docs/` 或 Python 即可编译；`docs/` 仍保持 Git 忽略。如需替换音频，在工程根目录运行：

```sh
python tools/wav_to_pcm.py "docs/播报语音/我摔倒了请帮帮我_8k.wav"
```

转换工具仅接受 8 kHz、单声道、16 位 PCM WAV，并检查一段完整立体声循环不超过 DMA 的 16 位传输计数上限。音频启动或 DMA 出错后，本次报警不反复重试；消警后再次触发可重新播放。播放由 DMA 异步完成，停止在主循环调用 HAL 完成末帧收尾；异常硬件状态下 HAL 停止可能等待超时。相关诊断由 `DEBUG_LOG_AUDIO` 和总开关控制。

转换参数 `--padding-ms` 可调整首尾保留的静音余量，`--keep-silence` 可保留完整原始时长。生成文件记录源 WAV 的 SHA-256、采样偏移与保留长度，便于核对。

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

当前总开关 `DEBUG_OUTPUT_ENABLE` 和所有 `DEBUG_LOG_*` 分类开关均为 `1`，USART1 输出全部分类的调试日志，包括周期时间信息。需要减少输出时，将对应分类设为 `0`；完全静默时将总开关设为 `0`。

日志统一使用类型前缀：`[BOOT]` 启动、`[TIM]` 定时器、`[KEY]` 按键、`[GPS]` GPS（含 GSV）、`[MPU]` 姿态传感器、`[OLED]` 显示、`[RTC]` 时间、`[ESP]` / `[ESP RX]` AT 通信、`[MQTT]` MQTT、`[REPORT]` 上报、`[FALL]` 报警、`[AUDIO]` 音频。

| 宏 | 输出内容 | 当前值 |
|---|---|---|
| `DEBUG_OUTPUT_ENABLE` | 所有调试日志的总开关 | 1 |
| `DEBUG_LOG_BOOT` | 启动横幅、时钟、接线提示 | 1 |
| `DEBUG_LOG_STATUS` | MPU 配置/零偏完成、OLED 初始化成功等状态 | 1 |
| `DEBUG_LOG_KEYS` | 按键操作 | 1 |
| `DEBUG_LOG_ERRORS` | 通用错误提示 | 1 |
| `DEBUG_LOG_FALL` | 报警、消警、扶正重新待命事件 | 1 |
| `DEBUG_LOG_AUDIO` | I2S2 语音启动、停止和错误 | 1 |
| `DEBUG_LOG_ESP` | AT 发送步骤、联网提示及 ESP 错误；凭据隐藏 | 1 |
| `DEBUG_LOG_ESP_RX` | ESP 返回的响应行；凭据命令回显隐藏 | 1 |
| `DEBUG_LOG_MQTT` | MQTT 连接、订阅、发布和重连状态 | 1 |
| `DEBUG_LOG_REPORT` | 上报 JSON、下行命令处理和上报周期 | 1 |
| `DEBUG_LOG_TIME` | RTC 启动、SNTP 校时及 RTC 写失败事件 | 1 |
| `DEBUG_LOG_TIME_PERIODIC` | 周期时间或未校准状态，需同时开启 `DEBUG_LOG_TIME` | 1 |
| `DEBUG_LOG_MPU_ACCEL` | 三轴加速度，单位 mg（1000 mg = 1 g） | 1 |
| `DEBUG_LOG_MPU_GYRO` | 三轴角速度，单位 mdps（1000 mdps = 1°/s） | 1 |
| `DEBUG_LOG_MPU_ANGLES` | Roll/Pitch，单位 cdeg（100 cdeg = 1°），以及采样间隔 dt/max（ms） | 1 |
| `DEBUG_LOG_GPS_FIX` | 定位有效性、历史位置标志、卫星数、位置年龄 | 1 |
| `DEBUG_LOG_GPS_POSITION` | 经纬度，单位为整数微度（除以 1000000 得到度） | 1 |
| `DEBUG_LOG_GPS_UTC` | GPS UTC 日期时间与有效标志 | 1 |
| `DEBUG_LOG_GPS_ALTITUDE` | 海拔（cm）、HDOP × 100 及有效标志 | 1 |
| `DEBUG_LOG_GPS_STATS` | NMEA 接收、解析、校验及串口错误计数 | 1 |
| `DEBUG_LOG_GPS_SIGNAL` | GSV 可见卫星、最大 C/N₀（dBHz）和信号档位 | 1 |

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
