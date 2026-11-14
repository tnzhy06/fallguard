# Fallguard · 跌倒守护

基于 STM32F103RCT6 的课堂手持跌倒报警演示工程，使用主循环调度，无 RTOS。

- MPU6050 中断采样与异步 I2C 读取，支持故障自动恢复；通过“快速倾倒后保持”的手持动作演示跌倒报警。
- ATGM336H 定位，ESP-01S 提供 Wi-Fi、SNTP 校时及 MQTT 上报。
- ST7789 中文界面与逐行启动日志，统一使用 HarmonyOS Sans；LSE 启动失败时允许降级运行。
- MAX98357A 播放求助语音，每遍间隔默认 1 秒；串口日志采用非阻塞发送。

## 接线

所有模块共地，UART 使用 3.3V 电平。

| 模块 | STM32 引脚 |
|---|---|
| MPU6050 | SCL PB6、SDA PB7、INT PA1，AD0 接地 |
| ST7789 | SCK PA5、MOSI PA7、CS PA4、DC PB4、RES PB5、BLK PB1 |
| ATGM336H | TXD → PA3，RXD → PA2（可选），115200 8N1 |
| ESP-01S | RXD → PB10、TXD → PB11、EN → PB0，115200 8N1 |
| MAX98357A | LRC PB12、BCLK PB13、DIN PB15 |
| K1 / K2 | PC8 / PC9，按下接地 |
| LED | PC7，低电平点亮 |
| 日志串口 | PA9 → USB 转串口 RXD，115200 8N1 |
| ST-LINK | SWDIO PA13、SWCLK PA14，NRST 可选 |

主时钟使用 8MHz HSE，RTC 使用 32.768kHz LSE。ESP-01S 需要稳定的 3.3V 电源；MAX98357A 使用 5V，扬声器跨接 OUT+、OUT−，任一输出均不能接地。

## 配置与构建

使用 STM32CubeIDE for VS Code，或安装 CMake 3.22+、Ninja、ARM GCC 并加入 PATH。

将 [App/app_config.example.h](App/app_config.example.h) 复制为 `App/app_config.h`，填写 Wi-Fi、MQTT 参数。设备编号、检测阈值、采样、语音间隔、日志、界面及上报参数均在此配置。

```sh
cmake --preset Debug
cmake --build --preset Debug
```

固件输出为 `build/Debug/fallguard.elf`。将两条命令中的 `Debug` 改为 `Release` 可构建发布版本；缺少本地配置时，CMake 会自动从模板创建。

VS Code 中按 **Ctrl+Shift+B** 构建 Debug，按 **F5** 使用 ST-LINK 烧录调试。任务菜单提供 Debug / Release 的 Configure、Build、Clean 和 Rebuild；构建前自动配置。

## 使用

上电后保持设备静止，等待姿态校准完成并进入主界面。**K1** 翻页，**K2** 测试报警或消警；消警后扶正设备恢复待命。

自动报警按以下动作演示，无需撞击或摔落设备：

1. **回正布防**：Roll、Pitch 都在 ±35°以内，稳定保持 0.3 秒。
2. **快速倾倒**：离开回正区后，在 0.3 秒内使 Roll 或 Pitch 的绝对值达到 60°。
3. **保持报警**：继续保持任一轴绝对值不低于 60°，持续 0.6 秒后报警。

超出动作窗口、保持时间不足或采样间隔达到 100ms 时，本次动作不触发报警，需稳定回正后重试。消警后保持倾倒不会重复报警。各阈值在 `app_config.h` 的 `APP_FALL_*` 分组调整；K2 可直接触发手动报警。

默认 MQTT 主题为 `fallguard/belt01/loc`、`fallguard/belt01/alarm` 和 `fallguard/belt01/cmd`。命令示例：

```json
{"cmd":"loc"}
{"cmd":"clear"}
{"cmd":"intv","v":10}
```

三条命令分别用于请求位置、消警和设置上报周期（1～3600 秒）。主题前缀和设备编号随统一配置变化。

## 目录与依赖

- `App/`：应用与音频资源；`BSP/`：外设驱动；`Core/`：初始化和中断。
- `Drivers/`：CubeMX 自动生成的 CMSIS/HAL，保持完整，不手工裁剪。
- `ThirdParty/`：驱动、字模及各自许可证；`tools/`：资源生成脚本。
- `fallguard.ioc`：CubeMX 配置；`.vscode/`：构建与调试配置。

`docs/`、`build/` 和包含本地凭据的 `App/app_config.h` 不纳入版本控制。

第三方来源与许可：[ST7789](ThirdParty/FloydFish_ST7789/README.md)、[MPU6050](ThirdParty/leech001_mpu6050/README.md)、[LwGPS](ThirdParty/lwgps/README.md)、[HarmonyOS Sans](ThirdParty/HarmonyOS_Sans/README.md)。
