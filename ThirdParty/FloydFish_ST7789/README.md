# ST7789 移植来源

- 上游：[Floyd-Fish/ST7789-STM32](https://github.com/Floyd-Fish/ST7789-STM32)
- 固定版本：`44ffcfefab97e1d225cbb9b1b4200cfd37093192`
- 许可证：GPL-3.0，原文见 [LICENSE](LICENSE)。

`st7789.c/.h` 基于上游的初始化寄存器、RGB565 格式、旋转和窗口寻址协议移植。
针对本项目的主循环调度改为 `Init/Poll/WriteRows/DisplayOn` 接口：

- 硬件复位和退出休眠采用 tick 状态机，不调用 `HAL_Delay`。
- 命令使用 2ms 有限超时，默认每块 16 行像素使用 SPI1 TX DMA；每帧 15 块，刷新缓冲区占用 7680 字节 RAM。
- 屏幕参数集中在 `App/app_config.h`，默认 240×240、旋转 2、RGB、反显开启；`st7789.h` 根据旋转参数计算地址偏移。
- DMA 传输期间保持 CS 为低，缓冲区直到传输完成才能复用。
- 传输超过 30ms 时终止传输，关闭背光，2 秒后重新初始化。
- 第一帧完整写入后才开启显示与背光。

未使用的上游 `fonts.c/.h` 与演示图片已移除；上游版本和许可保留在本目录。
绘图及中文渲染由 `BSP/bsp_lcd.c` 完成，不保留上游逐像素 SPI 绘图和阻塞演示接口。
中文、英文、数字与大时钟统一使用 HarmonyOS Sans SC，字模保存在 `../HarmonyOS_Sans/harmonyos_sans.h`，许可见 `../HarmonyOS_Sans/LICENSE.txt`。正式界面使用浅灰背景、白色圆角卡片和蓝色强调色；屏幕先显示逐行启动日志，再进入正式 UI。
