# Fallguard

基于 STM32F103RCT6 的跌倒报警演示项目，采用主循环调度，无 RTOS。

- MPU6050 检测姿态：Roll 或 Pitch 绝对值达到 60° 并持续 1 秒触发报警。
- ATGM336H 提供定位，ESP-01S 完成 Wi-Fi、SNTP 校时及 MQTT 上报。
- MAX98357A 通过 I2S2 播放“我摔倒了，请帮帮我”，每遍结束后间隔 1 秒再播放，ST7789 彩屏和 LED 同步提示。
- 1.54 寸 240×240 IPS 彩屏提供中文主页、位置、卫星、姿态、网络与报警界面。
- K1 切换彩屏页面；K2 测试报警或消警。消警后扶正设备，恢复自动报警待命。

项目开发规划、彩屏移植说明、工程流程图与 FreeRTOS 开发教程保存在本地 `docs/` 目录，该目录不纳入版本控制。

## 接线

表中为 GPIO 名称，所有模块共地，UART 使用 3.3V 电平。

| 模块 | 接线 |
|---|---|
| MPU6050 | SCL → PB6，SDA → PB7，INT → PA1，AD0 → GND；I2C1 400 kHz，地址 0x68 |
| ST7789 彩屏 | SCL/SCK → PA5，SDA/MOSI → PA7，CS → PA4，DC → PB4，RES → PB5，BLK → PB1；240×240 SPI 屏 |
| GPS | TXD → PA3，RXD → PA2（可不接）；115200 8N1，输出 GGA/RMC，信号强度需 GSV |
| ESP-01S | RXD → PB10，TXD → PB11，EN → PB0；115200 8N1 |
| MAX98357A | LRC/LRCLK → PB12，BCLK → PB13，DIN → PB15 |
| K1 / K2 | PC8 / PC9 → 按键 → GND，内部上拉 |
| LED | PC7，低电平点亮；正常慢闪，报警快闪 |
| 调试串口 | PA9 → USB 转串口 RXD，PA10 → TXD（可不接）；115200 8N1 |
| ST-LINK | SWDIO → PA13，SWCLK → PA14，GND → GND，NRST 可选 |

供电与连接要求：

- ESP-01S 使用稳定的 3.3V 电源，供电能力至少 500 mA；MPU6050、彩屏、GPS 使用模块支持的 3.3V 入口，I2C 上拉至 3.3V。彩屏背光接法见移植说明。
- MAX98357A 接 5V，4Ω / 3W 扬声器跨接 OUT+、OUT−，任一输出均不能接地。功放的 SD/SD_MODE 保持有效使能，GAIN 可用默认配置；PB15 接 DIN。
- ESP 的 RST、GPIO0、GPIO2 保持正常启动所需高电平；EN 由 PB0 控制，不直接短接 3.3V。
- 开发板需具备 8 MHz HSE 和 32.768 kHz LSE 晶振，正常启动时 BOOT0 为低。
- 若播放时 CH340 的 COM 口消失重现，优先检查供电压降及干扰。可用独立 5V 电源给功放供电并共地，先断开功放原来的 5V 连线，避免两路 5V 并联。

## 配置与构建

使用 CMake 3.22+、Ninja、ARM GCC；将工具加入 PATH，或使用 VS Code 的 STM32Cube 工具链环境。

1. 将 [app_config.example.h](App/app_config.example.h) 复制为 `App/app_config.h`，填写 Wi-Fi、MQTT 服务器及凭据。
2. 在同一文件中调整设备编号、检测阈值、采样与恢复、语音间隔、日志、显示、定位、上报及校时参数。
3. 在工程根目录构建：

```sh
cmake --preset Debug
cmake --build --preset Debug
```

固件输出为 `build/Debug/fallguard.elf`；将预设改为 `Release` 可构建发布版本。STM32Cube 环境也可使用 `cube-cmake`。VS Code 中按 Ctrl+Shift+B 构建，选择 `Fallguard: Debug (ST-LINK)` 后按 F5 烧录调试。

首次 CMake 配置会从模板自动创建缺失的 `App/app_config.h`。该本地配置文件和 `build/` 被 Git 忽略，仓库保存不含凭据的默认模板。所有连接参数在统一配置文件填写，参数修改后重新构建并烧录生效。

常用配置项集中在 `App/app_config.h`，各分组均附有中文注释和单位。

| 配置项 | 默认值 | 用途 |
|---|---|---|
| `APP_DEVICE_ID` / `APP_MQTT_TOPIC_PREFIX` | `belt01` / `fallguard` | 同步生成 JSON 设备编号、MQTT 客户端编号和各主题 |
| `APP_FALL_ANGLE_DEG` / `APP_FALL_HOLD_MS` | 60 / 1000 ms | 跌倒倾斜阈值与持续时间 |
| `BSP_MPU6050_RETRY_MS` / `BSP_MPU6050_SAMPLE_TIMEOUT_MS` | 2000 / 250 ms | 故障恢复间隔与数据过期检测 |
| `BSP_MPU6050_CALIBRATION_SAMPLES` | 200 帧 | 每次初始化和恢复时的陀螺仪校准数量 |
| `APP_AUDIO_REPEAT_GAP_MS` / `AUDIO_HALF_FRAMES` | 1000 ms / 256 帧 | 语音重复间隔与 DMA 半缓冲容量 |
| `DEBUG_OUTPUT_ENABLE` / `DEBUG_LOG_*` | 见模板 | 日志总开关和各类输出开关 |
| `DEBUG_TX_BUFFER_SIZE` / `DEBUG_TX_CHUNK_SIZE` | 2048 / 64 字节 | 非阻塞日志的队列容量和发送块长 |
| `APP_REPORT_DEFAULT_SECONDS` | 5 秒 | MQTT 默认上报周期 |
| `GPS_STALE_MS` / `GPS_SIGNAL_*_DBHZ` | 5000 ms / 30、40 dB-Hz | 定位过期与信号等级阈值 |
| `APP_TIMEZONE_HOURS` | +8 小时 | 同步控制 SNTP 请求、RTC 转换和界面时区 |
| `ST7789_*` / `LCD_BAND_ROWS` / `APP_UI_REFRESH_MS` | 见模板 | 屏幕旋转、颜色顺序、背光、分块和刷新周期 |

配置包含队列容量、采样数量、上报周期范围及 DMA 长度等编译期校验。当前界面按 240×240 屏幕布局，MCU 时钟、引脚、外设波特率与 DMA 通道由 `fallguard.ioc` 管理。PCM 采样率和长度由音频转换工具生成。

## 使用与调试

上电后保持 MPU6050 静止约 3 秒完成校准。按 K2 检查报警播放，再按 K2 停止；K1 切换主屏、位置、GPS、姿态和网络页面。GPS 模块波特率需自行配置为 115200，固件不自动修改。

MPU6050 使用 INT → PA1 / EXTI1 的 DATA_RDY 中断驱动采样。芯片 INT 为高电平有效并锁存，中断回调记录通知和时间戳，主循环从 INT_STATUS 连读 15 字节取得六轴和温度数据并清除锁存。默认 100 Hz，采样与滤波按中断时间戳推进。开源来源、寄存器设置与验证方法见 [MPU6050 INT 移植说明](ThirdParty/leech001_mpu6050/README.md)。

默认情况下，MPU6050 初始化或采样失败，以及连续 250 ms 没有有效中断样本时进入故障状态，每隔 2 秒自动尝试恢复。恢复流程释放 PB6/PB7 总线、最多发送 9 个 SCL 脉冲和 STOP，再重新初始化 I2C、复位并配置 MPU6050，重新使能 DATA_RDY 后采集 200 帧完成校准。恢复期间请保持设备静止，相关参数在 `App/app_config.h` 的 MPU6050 分组调整。可通过样本的 `irq_count` 或开启 `DEBUG_LOG_MPU_ACCEL` 后的 `irq=` 日志观察中断是否持续到达。

日志走 USART1，可通过板载 CH340 接收。统一配置中的 `DEBUG_OUTPUT_ENABLE=1`，分类开关可单独调整。默认 `printf` 将数据复制进 2 KiB 环形缓冲，主循环用 `HAL_UART_Transmit_IT` 每次发送最多 64 字节，完成中断释放缓冲。缓冲满时丢弃新写入的超出部分，`BSP_LOG_GetData()` 提供发送量、丢弃量和错误计数。发送超过 100 ms 未完成时放弃当前块并恢复后续发送。缓冲容量、发送块长和超时也在 `App/app_config.h` 调整。

## MQTT

默认设备编号 `belt01`，位置每 5 秒上报。本地时间显示 UTC+8，时间戳使用 UTC Unix 秒，未校准时为 0。

| 主题 | 用途 |
|---|---|
| `fallguard/belt01/loc` | 发布位置与状态：id、ts、lat、lon、fix、st（ok/fall） |
| `fallguard/belt01/alarm` | 发布事件：ev 为 fall 或 cancel |
| `fallguard/belt01/cmd` | 接收下表命令 |

| 命令 | JSON |
|---|---|
| 请求位置 | `{"cmd":"loc"}` |
| 消警 | `{"cmd":"clear"}` |
| 设置上报周期 | `{"cmd":"intv","v":10}`，允许 1～3600 秒，重启恢复 5 秒 |

经纬度为整数微度，除以 1000000 得到度；定位失效时可保留上次坐标，应结合 fix 判断。离线事件不补发，重连后上报当前状态。

## 音频与测试

报警语音为“我摔倒了，请帮帮我”，原 WAV 文件是 16 kHz、单声道、16 位 PCM。默认仅裁去首尾全零采样并各保留 20 ms，得到 29960 个单声道采样（按源采样率约 1.87 秒）。Flash 中只存这一份单声道 PCM，共 59920 字节；与预先复制左右声道的 119840 字节相比减少 50%。固件直接编译 `App/audio/alarm_pcm.c`，构建和运行无需解析 WAV 或使用 SD 卡。

播放时 `BSP/bsp_audio.c` 默认用 2 KiB RAM 双缓冲补齐 I2S 左右声道，左右内容仍完全相同。DMA 半传输和全传输回调分别补充刚播放完的一半，默认每次处理 256 个单声道采样，不调用阻塞 HAL 或日志函数。每次完整语音后插入静音采样，再从开头播放，DMA 持续运行。统一配置中的 `APP_AUDIO_REPEAT_GAP_MS=1000U` 默认生成 16000 个静音采样，按当前实际采样率约为 1 秒；设为 0 可连续循环。消警会立即停止语音或静音间隔。底层原有 `BSP_AUDIO_PlayLoop` 保持连续循环，带间隔播放使用 `BSP_AUDIO_PlayLoopWithGap`。停止时先禁止继续填充，再停止 DMA；异常停止仍由主循环执行。

`Core/Src/i2s.c`、播放格式检查和 `fallguard.ioc` 均配置为 `I2S_AUDIOFREQ_16K`、16 位 Philips I2S。当前 72 MHz 时钟整数分频得到的实际采样率约为 15.957 kHz，与 CubeMX 显示一致。

替换语音时准备 16 kHz、单声道、16 位 PCM WAV，再运行：

```sh
python tools/wav_to_pcm.py "path/to/alarm.wav"
python -m unittest discover -s tests -v
```

转换默认裁去首尾全零采样，各保留 20 ms 余量；不进行重采样、音量调整或中间静音裁剪。`--keep-silence` 可保留完整音频。源音频通过小缓冲区分块播放，不再受单次 DMA 的 65535 个数据长度限制；总长度使用 32 位采样计数，最终文件大小仍须满足 MCU 的 Flash 容量。转换后重新编译烧录。

测试包含 WAV 转换和实际 C 模块的主机模拟，覆盖音频左右时隙顺序、长音频和奇数采样长度、精确静音长度、DMA 缓冲区保护及间隔中消警。故障注入测试还覆盖 MPU 初始化失败、采样中断、总线卡住、重新校准、日志环形缓冲回绕、溢出和串口错误恢复。配置联动测试通过替换设备编号、检测阈值、上报周期与 UTC+8、UTC、UTC-5 时区，验证 JSON、MQTT AT 命令、RTC 和备份恢复的一致性。主机测试使用临时默认配置，不读取本地连接凭据。C 模块测试需要主机 C 编译器（GCC、Clang 或 TinyCC，可通过环境变量 `CC` 指定）；没有编译器时会报告跳过。最终仍需在开发板上试听并验证与彩屏、串口并发运行的时序。

## 资料与目录

- `App/`：应用逻辑；`BSP/`：外设驱动；`Core/`：CubeMX 初始化和中断。
- `Drivers/`：CMSIS/HAL；`ThirdParty/`：第三方代码及许可证。
- [fallguard.ioc](fallguard.ioc)：CubeMX 配置。
- 本地 `docs/`：硬件手册、设计资料、流程图和原始音频，由本地自行维护。
