# Fallguard

基于 STM32F103RCT6 的跌倒报警演示项目，采用主循环调度，无 RTOS。

- MPU6050 检测姿态：Roll 或 Pitch 绝对值达到 60° 并持续 1 秒触发报警。
- ATGM336H 提供定位，ESP-01S 完成 Wi-Fi、SNTP 校时及 MQTT 上报。
- MAX98357A 通过 I2S2 循环播放“我摔倒了，请帮帮我”，OLED 和 LED 同步提示。
- K1 切换 OLED 页面；K2 测试报警或消警。消警后扶正设备，恢复自动报警待命。

## 接线

表中为 GPIO 名称，所有模块共地，UART 使用 3.3V 电平。

| 模块 | 接线 |
|---|---|
| MPU6050 | SCL → PB6，SDA → PB7，AD0 → GND；I2C1 400 kHz，地址 0x68 |
| OLED | SCL → PB4，SDA → PB5；128×64 I2C 屏，地址 0x3C |
| GPS | TXD → PA3，RXD → PA2（可不接）；115200 8N1，输出 GGA/RMC，信号强度需 GSV |
| ESP-01S | RXD → PB10，TXD → PB11，EN → PB0；115200 8N1 |
| MAX98357A | LRC/LRCLK → PB12，BCLK → PB13，DIN → PB15 |
| K1 / K2 | PC8 / PC9 → 按键 → GND，内部上拉 |
| LED | PA8，低电平点亮；正常慢闪，报警快闪 |
| 调试串口 | PA9 → USB 转串口 RXD，PA10 → TXD（可不接）；115200 8N1 |
| ST-LINK | SWDIO → PA13，SWCLK → PA14，GND → GND，NRST 可选 |

供电与连接要求：

- ESP-01S 使用稳定的 3.3V 电源，供电能力至少 500 mA；MPU6050、OLED、GPS 使用模块支持的 3.3V 入口，I2C 上拉至 3.3V。
- MAX98357A 接 5V，4Ω / 3W 扬声器跨接 OUT+、OUT−，任一输出均不能接地。功放的 SD/SD_MODE 保持有效使能，GAIN 可用默认配置；PB15 接 DIN。
- ESP 的 RST、GPIO0、GPIO2 保持正常启动所需高电平；EN 由 PB0 控制，不直接短接 3.3V。
- 开发板需具备 8 MHz HSE 和 32.768 kHz LSE 晶振，正常启动时 BOOT0 为低。
- 若播放时 CH340 的 COM 口消失重现，优先检查供电压降及干扰。可用独立 5V 电源给功放供电并共地，先断开功放原来的 5V 连线，避免两路 5V 并联。

## 配置与构建

使用 CMake 3.22+、Ninja、ARM GCC；将工具加入 PATH，或使用 VS Code 的 STM32Cube 工具链环境。

1. 将 [esp_config.example.h](BSP/esp_config.example.h) 复制为 `BSP/esp_config.h`，填写 Wi-Fi。
2. 将 [mqtt_config.example.h](BSP/mqtt_config.example.h) 复制为 `BSP/mqtt_config.h`，填写 MQTT 服务器及凭据。
3. 在工程根目录构建：

```sh
cmake --preset Debug
cmake --build --preset Debug
```

固件输出为 `build/Debug/fallguard.elf`；将预设改为 `Release` 可构建发布版本。STM32Cube 环境也可使用 `cube-cmake`。VS Code 中按 Ctrl+Shift+B 构建，选择 `Fallguard: Debug (ST-LINK)` 后按 F5 烧录调试。

首次 CMake 配置会自动创建缺失的本地配置头文件。凭据和 `build/` 被 Git 忽略，`docs/` 随仓库保存。

## 使用与调试

上电后保持 MPU6050 静止约 3 秒完成校准。按 K2 检查报警播放，再按 K2 停止；K1 切换主屏、位置、GPS、姿态和网络页面。GPS 模块波特率需自行配置为 115200，固件不自动修改。

日志走 USART1，可通过板载 CH340 接收。 [BSP/debug_config.h](BSP/debug_config.h) 中 `DEBUG_OUTPUT_ENABLE=0`，所有分类开关为 1；开启总开关后重新编译烧录，即可输出各类日志。

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

语音为 8 kHz、16 位 PCM，由 I2S2 循环 DMA 从 Flash 播放，左右声道内容相同，单次约 1.87 秒。固件直接编译 `App/audio/alarm_pcm.c`，无需源 WAV 或 SD 卡。

替换语音时准备 8 kHz、单声道、16 位 PCM WAV，再运行：

```sh
python tools/wav_to_pcm.py "path/to/alarm_8k.wav"
python -m unittest discover -s tests -v
```

转换默认裁去首尾全零采样，各保留 20 ms 余量；`--keep-silence` 保留完整音频。转换后重新编译烧录。

## 资料与目录

- `App/`：应用逻辑；`BSP/`：外设驱动；`Core/`：CubeMX 初始化和中断。
- `Drivers/`：CMSIS/HAL；`ThirdParty/`：第三方代码及许可证。
- [fallguard.ioc](fallguard.ioc)：CubeMX 配置。
- [MAX98357A/MAX98357B 数据手册](docs/MAX98357A-MAX98357B.pdf)
- [STM32F103RC 数据手册](docs/ST-STM32F103RC.pdf)
- [ESP-01S 规格书](docs/ESP-01S/ESP-01S规格书-20250802.pdf)、[ESP-AT 用户指南](docs/ESP-01S/ESP-AT%20User%20Guide.pdf)
- [GPS 数据手册](docs/ATGM336H-5N/ATGM336H-5N模块数据手册.pdf)、[使用手册](docs/ATGM336H-5N/ATGM336H-5N模块使用手册.pdf)、[常见问题](docs/ATGM336H-5N/北斗模块%20%20常见问题必看必看.pdf)
- [开源项目链接](docs/开源项目.txt)
