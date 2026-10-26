# Fallguard

基于 STM32F103RCT6 的腰带式跌倒报警课程演示项目。采用主循环调度，无 RTOS、无 DMA。

- MPU6050：三轴加速度、角速度、Roll/Pitch；任一角度绝对值达到 60° 并持续 1 秒触发报警。
- ATGM336H：GPS 定位、UTC、海拔及信号强度。
- JQ8900：一线串口语音报警，使用根目录 `00001.mp3`。
- ESP-01S：Wi-Fi、SNTP 校时及 MQTT 上报。
- OLED：主屏、位置、GPS、姿态和网络五页；K1 切页，K2 测试报警/消警。

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

## 使用说明

上电后保持 MPU6050 静止约 3 秒，等待陀螺零偏校准。消警后需扶正至 Roll、Pitch 的绝对值均小于 60°，才会再次自动触发。

默认设备编号 `belt01`，MQTT 主题为 `fallguard/belt01/loc`、`fallguard/belt01/alarm`、`fallguard/belt01/cmd`。默认每 5 秒上报，命令允许设置 1～3600 秒；MQTT 不上报卫星数。调试输出在 `BSP/debug_config.h` 中按分类开关，USART1 为 115200 8N1。

第三方代码及许可证见 `ThirdParty/`。本项目用于课程演示。
