# zeruns OLED 移植记录

上游：https://github.com/zeruns/STM32G4-OLED-SSD1306-I2C-HAL
固定提交：4127e9defa9cfde4b82c87c8e3a4165dbfaa776c
上游目录：Core/Hardware/
许可：上游根 LICENSE（LGPL-3.0），本目录 LICENSE 原样保留；原代码的江协科技与 zeruns 作者声明也保留在派生源码中。

## 文件对应

| 当前文件 | 上游来源 |
|---|---|
| ../../BSP/bsp_oled.c | OLED.c 中显存、清除/反色、ASCII、位图、直线、矩形和圆形绘制；软件 I2C 和初始化按本项目适配 |
| oled_data.c | OLED_Data.c 的 ASCII 8x16/6x8、中文 16x16 和 Diode 位图；只整理初始化括号，字模数值与索引逐项一致 |
| oled_data.h | OLED_Data.h 的配套数据声明，使用新的头文件卫哨 |

## 本项目修改

- 外设为 STM32F103，使用 CubeMX 的 OLED_SCL/OLED_SDA 宏（PB4/PB5），不开硬件 I2C、不重新初始化引脚。
- 软件 I2C 继承上游起停、MSB 优先发送与 SSD1306 页/列定位方式；移除 G4 hi2c3 和忙循环延时，改用已启动 TIM2 的短微秒延时。
- SDA 在第九时钟前释放并读取 ACK；总线未释放或 NACK 时结束当前事务、报告错误，不反复重试或阻塞其他任务。
- 初始化供电等待为 tick 状态机；保留上游初始化参数，将 0xAF 延后到首帧完整传输后，避免显示未初始化 RAM。
- 上游 OLED_Update 整屏阻塞改为 BSP_OLED_Update 发起、BSP_OLED_Poll 每次最多发送 16 字节数据。单帧 64 块；绘图只在传输空闲时进行，沿用单个 1024 字节显存。
- 修正原 OLED_ShowChinese 的 SingleChinese[pChinese + 1] 越界写；保留 UTF-8 三字节索引及未收录字的方框问号。
- 修正 ASCII 索引、长字符串横坐标回绕、空位图、非 8 倍数高度位图、矩形边界，以及圆形负坐标先窄化造成的回绕。
- 不移植未用到的浮点显示、vsprintf、不需要的几何接口和硬件 I2C 分支。BSP 对外接口按工程规范改为 BSP_OLED_*。

中文字库目前只包含“你、好、世、界、，、。”及缺字占位图，不是完整中文字库；其他业务汉字待后续 UI 阶段按需补齐。

## 上游源文件 SHA-256

OLED.c: 00a06b4989fd61103f2ca465608af1913d73edb5e888aaaa5c78fa8de8a0dc12

OLED.h: 7da4eb6e953d929af22bb827378458431543e141d244558d34471a05af0f4849

OLED_Data.c: 3a97f1bb27a1040a822fcd36f554bad0adb3e654e2719a7b537e220f6f2d95b3

OLED_Data.h: e0af7461a263288fffcc9cb03b76836c2f218b24962657b4b656a34c91f9e8c9

