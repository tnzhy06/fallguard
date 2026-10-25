# 开发须知（给 AI Agent 阅读）

> 配套文档在 `docs/` 目录

---

## 1. 项目是什么

一条腰带式可穿戴装置，面向生活不便者：

- 用 **ATGM336H** 拿 GPS 定位（解析 NMEA）
- 用 **MPU6050** 读取三轴加速度、三轴角速度和 Roll/Pitch（移植 leech001），在 OLED 与调试串口实时显示；已加入用户确认的简单倾斜跌倒演示。
- 跌倒时用 **JQ8900** 播中文语音报警
- 用 **ESP-01S**（已刷入 **1471 号固件**）通过 **MQTT** 把位置和状态以 **JSON** 上报服务器
- 用 **0.96" OLED** 显示时间/网络/GPS 信号/位置
- 两个按键 + 一个 LED 做本地交互

**这是课程展示项目，不是商业产品。** 目标是功能跑通、代码简洁、便于讲解。

---

## 2. 铁律（违反会被打回）

1. **引脚不许自作主张改。** 见第 3 节表格。要改先问用户。
2. **不使用 RTOS。** 主循环 + `HAL_GetTick()` 时间片轮询。
3. **不使用 DMA。** 三个串口都用单字节中断接收 + 环形缓冲。（数据率很低，DMA 是过度设计）
4. **不引入需要 malloc 的库**（cJSON 等）。JSON 上行用固定缓冲区拼装（`snprintf` 并检查长度），下行仅在完整、以 NUL 终止的载荷副本上用 `strstr` 定位字段并校验值。
5. **不用浮点 printf**（不要 `-u _printf_float`）。经纬度以**整数微度**传输。
6. **手工修改 `Core/` 中 CubeMX 生成的 C/H 文件时，只改成对的 `/* USER CODE BEGIN xxx */` / `/* USER CODE END xxx */` 块。** 外设配置通过 CubeMX 修改 `.ioc` 后重新生成；根 `CMakeLists.txt`、用户维护的构建配置及 `BSP/`、`App/`、`ThirdParty/` 不受 USER CODE 块限制。不要手改 HAL 或生成的 `cmake/stm32cubemx/`。
7. **不要顺手重构、不要加用不上的抽象。** 三行相似代码好过一个用不上的函数。
8. **涉及源码或构建配置的修改，必须编译通过再交付。** 编译不过不要说固件已完成；仅文档修改需核对引用、示例及一致性，不能声称通过硬件验证。

---

## 3. 引脚表（唯一依据，来自 `约束.md` + 开发文档 3.2）

| 引脚 | 功能 | 外设 | 参数 | 来源 |
|---|---|---|---|---|
| PA9 / PA10 | 调试串口 | USART1 | 115200 8N1 | 约束 |
| PA2 / PA3 | GPS | USART2 | 115200 8N1 | 用户已将 ATGM336H 模块设为 115200；开发文档 |
| PB10 / PB11 | ESP-01S | USART3 | 115200 8N1 | 开发文档 |
| PB12 | ESP-01S EN | GPIO 输出 | 默认高 | 开发文档 |
| PB6 / PB7 | MPU6050 | I2C1 | 400 kHz | 开发文档 |
| PB4 / PB5 | OLED SCL/SDA | GPIO **开漏+外部上拉至 3.3 V** | 软件 I2C | 约束 |
| PB0 | JQ8900 一线串口 | GPIO 输出 | 空闲高 | 开发文档 |
| PB1 | JQ8900 BUSY | GPIO 输入 | 播放高、空闲低；C07 已确认接入 | 开发文档 |
| PA8 | LED | GPIO 输出 | **低电平点亮** | 约束 |
| PC8 / PC9 | 按键 K1/K2 | GPIO 输入 | **低电平触发**，上拉 | 约束 |
| PC14 / PC15 | RTC 晶振 | LSE | 32.768 kHz | 约束 |
| PA0 | **不用** | — | — | 约束 |

**User Label**（CubeMX 生成的引脚宏，已定义在 `Core/Inc/main.h` 里，代码里直接用）：

| 宏 | 对应引脚 | 宏 | 对应引脚 |
|---|---|---|---|
| `LED_Pin` / `LED_GPIO_Port` | PA8 | `KEY1_Pin` / `KEY1_GPIO_Port` | PC8 |
| `JQ_SDA_Pin` / `JQ_SDA_GPIO_Port` | PB0 | `KEY2_Pin` / `KEY2_GPIO_Port` | PC9 |
| `ESP_EN_Pin` / `ESP_EN_GPIO_Port` | PB12 | `JQ_BUSY_Pin` / `JQ_BUSY_GPIO_Port` | PB1 |
| `OLED_SCL_Pin` / `OLED_SCL_GPIO_Port` | PB4 | `OLED_SDA_Pin` / `OLED_SDA_GPIO_Port` | PB5 |

**一律用宏**：写 `HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET)`，
不要硬写 `HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, ...)`。外设（USART/I2C）用句柄 `huart1`/`huart2`/`huart3`/`hi2c1`。

---

## 4. 工程结构与各层职责

```
fallguard/
├─ Core/          CubeMX 生成，只改 USER CODE 块
├─ Drivers/       HAL，不要动
├─ BSP/           外设驱动，只依赖 HAL
│   ├─ bsp_delay.c/h     短 µs 延时（TIM2 计数器）
│   ├─ bsp_rtc.c/h       RTC 日期时间读写
│   ├─ bsp_led.c/h       PA8
│   ├─ bsp_key.c/h       PC8/PC9，含消抖
│   ├─ bsp_oled.c/h      SSD1306，软件 I2C（移植 zeruns）
│   ├─ bsp_mpu6050.c/h   I2C1（移植 leech001）
│   ├─ bsp_gps.c/h       USART2 中断收 + 环形缓冲
│   ├─ bsp_esp.c/h       USART3，AT 指令状态机
│   └─ bsp_jq8900.c/h    一线串口时序
├─ App/           业务逻辑，只依赖 BSP
│   ├─ app_main.c/h      调度器 + 系统状态机
│   ├─ app_fall.c/h      60°持续1秒的简单倾斜演示
│   ├─ app_voice.c/h     C07 报警语音与 BUSY 重播
│   ├─ app_time.c/h      C08 SNTP 校时与 RTC 调度
│   ├─ app_report.c/h    上报调度
│   ├─ app_ui.c/h        OLED 界面
│   └─ app_json.c/h      JSON 拼装/解析
└─ ThirdParty/    lwgps/、所选 OLED 驱动配套字库
```

**依赖方向**：`App → BSP → HAL`，第三方库由对应 BSP 封装。BSP 不许调 App；App 不直接操作外设句柄或寄存器，RTC 通过 `bsp_rtc` 读写。仅允许 App 调度及时间戳轮询使用 `HAL_GetTick()`，不为这一项增加时钟抽象。Core 的 USER CODE 块负责初始化和中断接收接入。

**协议依据**：ESP-01S 已刷入 **1471 号固件**；这个编号不等于已确认的 AT/SDK 版本。AT MQTT/SNTP 语法以 `docs/ESP-AT User Guide.pdf` 为准，保留 MQTT 的 `LinkID=0`，不切换到所谓“去掉 LinkID 的 NonOS MQTT”方案。JQ8900 一线协议以 `docs/JQ8900语音模块16p.pdf` 为准；图与示例不一致处见开发文档 5.3，需实测后定稿。

C07 当前实现：PB0 接 VPP、PB1 接 BUSY，TIM3 1 MHz 中断推进一线波形，1 ms 高/4 ms 引导低/500 与 1500 µs 数据位，字节间隔至少 12 ms。用户确认 FLASH 根目录 00001.mp3，自动与手动报警共用，K2 消警发 0x13；BUSY 高转低后按报警状态重播。未用一线循环模式数值。2026-10-26 用户确认 C07 验收通过；未提供示波器测量值，不补写实测结论，详见 `docs/C07验收.md`。

C08 第一部分：USART3 单字节中断+环形缓冲、AT 中断发送、Wi-Fi/SNTP 非阻塞状态机；本地凭据在被 Git 忽略的 `BSP/esp_config_local.h`，空配置仍探测 AT/GMR。按手册 `CIPSNTPCFG=1,8` 获取东八区日历，通过 `bsp_rtc` 写入，GPS UTC 不写 RTC。F1 RTC 使用 BKP DR1～DR4 保存午夜 UTC 基准与校验，复位后恢复累计秒；后续不能另行调用 HAL_RTC_GetTime/SetDate 改动计数器。用户已提供 GMR（AT 2.3.0.0-dev / Bin 2.2.0 ESP8266_1MB）并确认 Wi-Fi GOT IP；单服务器曾返回 1970，改用 aliyun/sjtu/ntp.org.cn 三服务器后用户确认校时可用；VBAT 尚无单独实测记录，见 `docs/C08校时与RTC验收.md`。本轮未接入 MQTT。

GPS C05 已确认：ATGM336H 与 USART2 均为 115200 8N1；GGA/RMC 有效位置与 GSV 信号采用 5 秒过期规则。信号按最大 C/N₀ 分为无（无效/过期/0）、弱（1～29）、中（30～39）、强（40～99 dBHz），不参与 fix 判定；OLED 仅显示 0～3 个信号格，不显示四档中文文字。展示 UTC、经纬度、海拔、卫星数和 HDOP，不展示速度；UTC 不写 RTC。

---

## 5. 代码规范

- 文件名、App 函数和模块内部函数用 `小写_下划线`；BSP 对外接口作为命名例外，统一 `BSP_模块_动作()`，如 `BSP_OLED_ShowString()`。
- 每个模块一对 `.c/.h`，头文件用 `#ifndef` 卫哨。
- **注释只写"为什么"，不写"是什么"。** 比如"按载荷长度截取，避免把 JSON 内部换行当成帧尾"是好的；"// 设置引脚为输出"是废话。
- **禁止阻塞式长延时**（`HAL_Delay(1000)` 之类）出现在业务逻辑里。要做延时/等待，一律拆成状态机或用 `HAL_GetTick()` 记时间戳。
- 全局变量尽量少，模块内部状态用 `static`。
- 调试输出集中在 `BSP/debug_config.h`，新增日志用 `DEBUG_PRINT` 及对应分类；当前仅输出 C08 ESP 步骤、原始响应与 RTC 时间/校时，其他分类及通用错误关闭；ESP/RTC 自身错误随 ESP/TIME 分类保留，关闭输出不影响采样、接收和显示。
- UART 中断里只搬数据、重新挂接收和处理必要接收错误，不解析、不 printf。若 C07 使用定时器驱动一线波形，定时器中断只推进边沿，不执行业务。
- JQ8900 的长引导码和字节间隔不能用业务阻塞延时，也不能整字节关闭全局中断；不引入 DMA，不擅自改成 IO 触发或两线串口。

---

## 6. 工作方式（重要）

- **一次只做一块。** 按开发文档 C01~C10 的顺序推进，**做完一块、编译通过、交给用户验证，再开始下一块**。不要一口气把 C01~C10 全写完。
- **顺序原则**：先把不依赖网络的本地功能做完（C01~C07），最后做网络（C08~C09）。这样网络卡住也不影响演示。
- **改引脚、改协议格式、改阈值语义之前先问用户**，不要自己决定。
- **不确定的硬件行为先让用户测。** 比如 JQ8900 实际波形是否被识别、LSE 是否起振，这些你测不了，要让用户拿万用表/示波器确认。
- **最新用户已确认的演示规则**：|Roll| 或 |Pitch| ≥60° 连续 1 秒即锁定本地报警，K2 消警后须先两角均 <60° 才能再次自动触发；保留 K2 手动报警。这替代原三段判据和旧等待窗口，慢放倒也会触发。无需六面采集，按课程演示功能验收。
- 历史失重/冲击/阶段③方案均不生效；改变当前角度、保持时间或复位语义仍须先确认。MPU6050 沿用 PB6/PB7、I2C1 400 kHz、AD0=0（0x68）、±2 g/±250°/s；启动静置约 3 秒采集陀螺零偏。
- **交付时说清楚**：改了什么文件、编译过没有、哪些还没验证。不要含糊说"完成了"。

---

## 7. 不要做的事

- ❌ 上 FreeRTOS / RT-Thread
- ❌ 上 DMA（串口、I2C 都不需要）
- ❌ 引 cJSON、引任何用 malloc 的库
- ❌ 用 `%.6f` 打印浮点（用整数微度）
- ❌ 改 `Core/` 里 `USER CODE` 块外面的代码
- ❌ 改引脚、改 MQTT topic 命名、改 JSON 字段名（要改先问）
- ❌ 硬写 `GPIOA, GPIO_PIN_8`（要用 CubeMX 生成的 `LED_Pin` 这类宏）
- ❌ 写"以防万一"的错误处理、兼容层、抽象层
- ❌ 写解释"是什么"的注释
