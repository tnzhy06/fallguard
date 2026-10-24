# STM32CubeMX 配置指南 — 弱势群体定位报警腰带

> 照着本文一步步做，最后点 Generate Code 就能得到可编译的工程骨架。
> 引脚依据 `约束.md`、`../AGENTS.md` 与 `项目开发文档.md` 第 3 章，按现有 `fallguard.ioc` 核对。
> ESP-01S 已刷入 **1471 号固件**，不以重新刷机作为前置；JQ8900 与 AT MQTT 时序/指令分别以随附两份 PDF 为准，实测步骤见开发文档 5.3/5.4。

---

## 0. 前置准备

| 项目 | 要求 |
|---|---|
| STM32CubeMX | 使用支持直接生成 CMake 的版本；现有 `fallguard.ioc` 记录版本为 6.18.1 |
| 固件包 | STM32Cube FW_F1 V1.8.x（首次用会让你下载） |
| 目标芯片 | **STM32F103RCTx**（LQFP64，256 KB Flash / 48 KB RAM） |
| 工程路径 | 建议 `D:/STM32CubeMX_Project/fallguard`，**路径里不要有中文和空格** |

启动 CubeMX → **File → New Project** → 在 "Part Number" 搜索框输入 `STM32F103RCT` → 选中 **STM32F103RCTx**（LQFP64 封装那个）→ 双击 → 弹出 "Are you sure..." 选 **Yes**（初始化所有外设为默认状态）。

现在你会看到芯片引脚图。下面按顺序配。

---

## 1. System Core → SYS（⚠️ 最容易漏，漏了 OLED 点不亮）

左侧 **System Core → SYS**：

| 项 | 设置 |
|---|---|
| Debug | **Serial Wire** |

**为什么必须选 Serial Wire**：默认是 "No Debug"。选了 Serial Wire 之后，CubeMX 会自动生成关闭 JTAG 的代码（`__HAL_AFIO_REMAP_SWJ_NOJTAG()`），**PB4 才能当普通 GPIO 用**。我们要用 PB4 接 OLED 的 SCL，所以这一步不能省。

> 选了 Serial Wire 之后 SWD 调试口（PA13/PA14）照常可用，ST-Link 下载不受影响。

Timebase Source 保持 **SysTick**（不用 RTOS，不需要换）。

---

## 2. System Core → RCC

| 项 | 设置 |
|---|---|
| High Speed Clock (HSE) | **Crystal/Ceramic Resonator** |
| Low Speed Clock (LSE) | **Crystal/Ceramic Resonator** |

本项目 RTC 选 LSE（板上 32.768 kHz 晶振）；晶振是否起振仍需实测。这里固定使用 LSE，不擅自切换其他时钟源。

设完之后，芯片图上 PC14/PC15 会变绿（被 LSE 占用）。

---

## 3. Clock Configuration（时钟树）

切到顶部 **Clock Configuration** 标签页，按下面配：

| 节点 | 值 |
|---|---|
| Input frequency | 8 MHz（HSE 晶振） |
| PLL Source Mux | **HSE** |
| PLLMul | **×9** |
| System Clock Mux | **PLLCLK** |
| HCLK (AHB Prescaler) | /1 → **72 MHz** |
| APB1 Prescaler | **/2** → 36 MHz |
| APB2 Prescaler | **/1** → 72 MHz |
| ADC Prescaler | /6 → 12 MHz |

配完顶部应该显示 **HCLK = 72 MHz**，下面几个红字警告全部消失。

**RTC 时钟源**：在时钟树里找到 RTC 那一栏，把 RTC Source 选成 **LSE**，确认显示 32.768 kHz。

> ⚠️ **F103 的坑**：F103 的 RTC 不像 F4 那样有独立时钟树，它是"备份域"的一部分。如果之前烧过别的程序、RTC 已经用别的时钟源跑起来过，这里配好也可能初始化失败。排查办法见第 10 节。

---

## 4. 引脚与外设配置

### 4.0 先给每个 GPIO 引脚设 User Label（强烈建议，先做这步）

**User Label 是什么**：CubeMX 允许给每个引脚起一个"用户标签"。设了之后，生成的 `Core/Inc/main.h` 里会自动出现对应的宏：

```c
/* Private defines -----------------------------------------------------------*/
#define LED_Pin        GPIO_PIN_8
#define LED_GPIO_Port  GPIOA
```

于是代码可以写成：

```c
HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);   /* 点亮 LED */

if (HAL_GPIO_ReadPin(KEY1_GPIO_Port, KEY1_Pin) == GPIO_PIN_RESET) { /* K1 按下 */ }
```

而不是 `HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, ...)`。

**好处**：代码自解释、不会记错引脚号；以后万一要换引脚，只改 CubeMX 一处，代码一个字都不用动。

**怎么设**：在芯片图上**右键点引脚 → Enter User Label**，或者到 **System Core → GPIO** 面板，最右边那一列 **User Label** 直接双击填。

**命名要求**：必须是合法的 C 标识符——只能字母/数字/下划线、不能以数字开头、**不能用中文、不能有空格**，且不能重名。

**本项目要设的标签**（照着填）：

| 引脚   | 用途              | User Label | 生成的宏                                  |
| ---- | --------------- | ---------- | ------------------------------------- |
| PA8  | LED             | `LED`      | `LED_Pin` / `LED_GPIO_Port`           |
| PB0  | JQ8900 一线串口 SDA | `JQ_SDA`   | `JQ_SDA_Pin` / `JQ_SDA_GPIO_Port`     |
| PB12 | ESP-01S EN      | `ESP_EN`   | `ESP_EN_Pin` / `ESP_EN_GPIO_Port`     |
| PC8  | 按键 K1           | `KEY1`     | `KEY1_Pin` / `KEY1_GPIO_Port`         |
| PC9  | 按键 K2           | `KEY2`     | `KEY2_Pin` / `KEY2_GPIO_Port`         |
| PB1  | JQ8900 BUSY（可选） | `JQ_BUSY`  | `JQ_BUSY_Pin` / `JQ_BUSY_GPIO_Port`   |
| PB4  | OLED SCL        | `OLED_SCL` | `OLED_SCL_Pin` / `OLED_SCL_GPIO_Port` |
| PB5  | OLED SDA        | `OLED_SDA` | `OLED_SDA_Pin` / `OLED_SDA_GPIO_Port` |

> **外设引脚（USART1/2/3、I2C1、LSE）不用设**。CubeMX 已经用 `huart1`、`hi2c1` 这些句柄管理它们了，再设标签没意义，还容易和 HAL 内部命名打架。
>
> 标签设完如果 `main.h` 里没出现这些宏，检查一下是不是标签名里有中文或空格——CubeMX 不会报错，只是静默不生成。

### 4.1 GPIO 输出

在芯片图上**左键点击引脚**选功能，然后到 **System Core → GPIO** 里设详细参数。

| 引脚       | 用途                | **User Label** | GPIO mode        | GPIO Pull-up/Pull-down | 初始电平     | 备注                  |
| -------- | ----------------- | -------------- | ---------------- | ---------------------- | -------- | ------------------- |
| **PA8**  | LED               | `LED`          | Output Push Pull | No pull                | **High** | 低电平点亮，所以初始 High = 灭 |
| **PB0**  | JQ8900 一线串口 SDA   | `JQ_SDA`       | Output Push Pull | No pull                | **High** | 空闲必须高电平             |
| **PB12** | ESP-01S EN(CH_PD) | `ESP_EN`       | Output Push Pull | No pull                | **High** | 默认拉高模块才工作           |

三个引脚的 **Maximum output speed** 均设 **Low**；JQ8900 的实际脉宽按开发文档 5.3 与手册实测，不用 GPIO speed 值代替协议定时。

### 4.2 GPIO 输入

| 引脚      | 用途              | **User Label** | GPIO mode  | Pull-up/Pull-down         |
| ------- | --------------- | -------------- | ---------- | ------------------------- |
| **PC8** | 按键 K1           | `KEY1`         | Input mode | **Pull-up**（低电平触发，所以必须上拉） |
| **PC9** | 按键 K2           | `KEY2`         | Input mode | **Pull-up**               |
| **PB1** | JQ8900 BUSY（可选） | `JQ_BUSY`      | Input mode | **Pull-up**               |

> PB1 是可选的，当前工程已配置。JQ8900 手册第 4 页明确 BUSY **有音频输出时高、无输出时低**，接线后核对实物。未接 PB1 时忽略其输入，不把上拉读到的高电平误认为正在播放。

### 4.3 OLED 软件 I2C 引脚（⚠️ 特别注意）

| 引脚 | 用途 | User Label | GPIO mode | GPIO Pull | Speed | 初始电平 |
|---|---|---|---|---|---|---|
| PB4 | OLED SCL | `OLED_SCL` | Output Open Drain | No pull（输出模式下内部上拉不生效） | Low (2 MHz) | High |
| PB5 | OLED SDA | `OLED_SDA` | Output Open Drain | No pull（输出模式下内部上拉不生效） | Low (2 MHz) | High |

软件 I2C 使用开漏，输出 1 表示释放总线，外部电阻将线路拉高。**STM32F1 的开漏输出不能同时开启内部上拉**：本工程 HAL 输出模式不使用 `GPIO_InitStruct.Pull`。现有 .ioc/生成代码虽存有 GPIO_PULLUP，也不能据此认为硬件已有上拉；后续用 CubeMX 整理该配置，不手改 Core 块外代码。

断电后检查模块 SCL/SDA 到 3.3 V 的上拉电阻；若没有，需要外接，短线连接可先用 4.7 kΩ，再按实际波形核对上升时间。若模块已有上拉，还要确认上拉电源确实为 3.3 V，不能只看模块电源引脚名称。

Low (2 MHz) 是 GPIO 驱动速度设置，不等于 2 MHz I2C；软件 I2C 的速率由延时和执行开销决定。上升沿主要受上拉和总线电容影响，实际速率与波形必须测量。

PB4/PB5 不具备 F103 硬件 I2C 复用功能，因此使用 zeruns 的软件 I2C 模式。开漏发送 1 后可读 SDA 输入状态，不必为 ACK 读取切成推挽输出。界面分批传输；100 kHz 下整屏数据传输约 92 ms，不能每次刷新阻塞主循环整屏发送。

### 4.4 串口（三个）

左侧 **Connectivity → USART1 / USART2 / USART3**，每个都选 **Asynchronous** 模式。

| 外设 | 引脚 | Baud Rate | Word Length | Parity | Stop Bits | 用途 |
|---|---|---|---|---|---|---|
| **USART1** | PA9(TX) / PA10(RX) | **115200** | 8 Bits | None | 1 | 调试串口，printf |
| **USART2** | PA2(TX) / PA3(RX) | **9600** | 8 Bits | None | 1 | GPS ATGM336H |
| **USART3** | PB10(TX) / PB11(RX) | **115200** | 8 Bits | None | 1 | ESP-01S |

三个的 **Hardware Flow Control (RS485/RS232)** 都保持 **Disable**。

**⚠️ 不要勾 DMA Settings。** 本项目三个串口都用单字节中断接收，不需要 DMA（原因见开发文档 4.3）。

> ATGM336H 默认就是 9600，如果它的 E2PROM 里被改过波特率，就按实际值改。

### 4.5 I2C1（MPU6050）

左侧 **Connectivity → I2C1** → **I2C**。

| 项 | 值 |
|---|---|
| I2C Speed Mode | **Fast Mode** |
| I2C Clock Speed | **400000** Hz |

引脚自动落到 **PB6(SCL) / PB7(SDA)**，符合我们的规划。

> I2C1 使用复用开漏、No pull；核对实际 GY-521 板上是否已有到 3.3 V 的上拉，不能假定所有模块均有固定的 4.7 kΩ 电阻。

### 4.6 TIM2（µs 延时）

左侧 **Timers → TIM2**：

| 项                   | 值                  |
| ------------------- | ------------------ |
| Clock Source        | **Internal Clock** |
| Prescaler           | **71**             |
| Counter Period      | **65535**          |
| auto-reload preload | Enable             |

**原理**：72 MHz ÷ (71+1) = **1 MHz**，即计数器每 1 µs 加 1，回绕周期 65.5 ms。

用 TIM2 做短微秒延时：先记下 CNT，再用 **16 位无符号差值**处理 65535→0 回绕。仅用于短 GPIO 脉冲等操作，不把毫秒等待串成阻塞业务。JQ8900 引导/字节间隔需让出主循环；如需定时器中断推进边沿，在 C07 单独确定定时器配置，不能直接把本文的无中断延时计数器配置当作完整波形发生器。

**⚠️ 不要在 NVIC 里给 TIM2 开中断** —— 我们只用它的计数器，不用中断。

> 本项目统一按 TIM2 计数器实现短延时，避免同时维护另一套延时来源。

### 4.7 RTC

左侧 **Timers → RTC**：

| 项 | 设置 |
|---|---|
| Activate Clock Source | **勾上** |

使用 LSE 与 `RTC_AUTO_1_SECOND`（等效 32767 分频得到 1 Hz），不启用本项目未使用的 RTC 校准/闹钟输出。现有生成代码的输出配置需在 C01 核对，若需调整通过 CubeMX 重新生成。

> 上电后由 ESP-01S 通过 SNTP 校时，再写进 RTC，之后 RTC 靠 LSE 独立走时。

---

## 5. NVIC 中断优先级

按当前 `fallguard.ioc` 核对，保持三个 UART 同优先级：

| 中断 | Enabled | Preemption Priority | Sub Priority |
|---|---|---|---|
| USART1 global interrupt | 配置已开，接收按测试命令需求挂接 | 0 | 0 |
| USART2 global interrupt | 是 | 0 | 0 |
| USART3 global interrupt | 是 | 0 | 0 |
| SysTick | 是 | 15 | 0 |

三个 UART 不互相抢占，回调只搬数据；SysTick 维持 HAL 时间基准。系统异常沿用生成配置，不能把不可编程的 HardFault 优先级与可配置异常混成一项设置。

不要给 TIM2 延时计数器开中断，不开 DMA。若 C07 引入另一个定时器推进 JQ8900 边沿，再核对其优先级和最坏响应时间，通过 CubeMX 配置。

**不得整字节用 `__disable_irq()` 发送 JQ8900**：这会丢失 UART 字节、合并 SysTick 待处理中断并导致调度时间失真。手册中 C51 的关中断示例不能直接搬来当 STM32 的并发方案。按开发文档 5.3 测量脉宽和接收完整性，不能把丢包解释为“不影响功能”。

---

## 6. Project Manager 工程设置

### 6.1 Project 标签页

| 项 | 值 |
|---|---|
| Project Name | `fallguard` |
| Project Location | `D:/STM32CubeMX_Project`（**不要中文、不要空格**） |
| Application Structure | **Advanced** |
| Toolchain / IDE | **CMake** |

> 若版本没有 CMake 选项，使用支持 CMake 生成的 CubeMX 版本打开现有工程。第 8.3 节仅是向现有 CMake 添加源码的示例，不是 Makefile 转 CMake 的完整教程。

### 6.2 Code Generator 标签页

| 项 | 设置 |
|---|---|
| Copy only the necessary library files | **✅ 勾上**（工程更小） |
| Generate peripheral initialization as a pair of .c/.h files per peripheral | **✅ 勾上** |
| Delete previously generated files when not re-generated | 可不勾 |
| Set all free pins as analog (to optimize power consumption) | **不勾** |
| Generate Under Root | 默认 |

**为什么勾 "a pair of .c/.h per peripheral"**：这样 `gpio.c`、`usart.c`、`i2c.c`、`rtc.c`、`tim.c` 各自独立，比全塞在 `main.c` 里清楚得多，我们后面加代码也不容易和 CubeMX 重新生成的代码打架。

### 6.3 Advanced Settings 标签页

一般不用动。只有一点：如果生成的代码里 USART 的 GPIO 速度是 Low，且你后面遇到串口波形不好的问题，可以回来把对应 USART 的 GPIO 速度调到 **High**。

---

## 7. 生成代码

点右上角 **GENERATE CODE**。

第一次会提示下载固件包，点下载等它装完。生成完成后点 **Open Project**（如果装了 VSCode + CMake Tools，会直接打开工程）。

生成完的目录结构大致是：

```
fallguard/
├─ CMakeLists.txt
├─ cmake/                    工具链文件
├─ startup_stm32f103xe.s
├─ STM32F103xx_FLASH.ld
├─ Core/
│   ├─ Inc/  main.h  gpio.h  usart.h  i2c.h  rtc.h  tim.h  stm32f1xx_it.h ...
│   └─ Src/  main.c  gpio.c  usart.c  i2c.c  rtc.c  tim.c  stm32f1xx_it.c ...
└─ Drivers/
    ├─ CMSIS/
    └─ STM32F1xx_HAL_Driver/
```

---

## 8. 生成后的接入与构建

### 8.1 printf 重定向到调试串口

在 `Core/Src/main.c` 里找到 `/* USER CODE BEGIN Includes */`，加一行：

```c
#include <stdio.h>
```

再找到 `/* USER CODE BEGIN 4 */` 和 `/* USER CODE END 4 */`，在这两块之间加：

```c
int _write(int file, char *ptr, int len)
{
    (void)file;
    HAL_UART_Transmit(&huart1, (uint8_t *)ptr, (uint16_t)len, HAL_MAX_DELAY);
    return len;
}
```

最后在 `main()` 的 `/* USER CODE BEGIN 2 */` 里加：

```c
setvbuf(stdout, NULL, _IONBF, 0);
printf("\r\n=== fallguard boot ===\r\n");
```

本工程 GCC/newlib 的 `syscalls.c` 已提供 weak `_write`，USER CODE 中提供一个 strong `_write` 即可覆盖；若改用 `__io_putchar`，则不要再保留第二套重定向。仅在主循环输出短日志，不能在 UART 接收中断内 printf；示例中的 HAL_MAX_DELAY 在 C01 验证后应换成有限超时并检查发送结果，避免串口故障阻塞本地任务。

**注意**：我们全程不用浮点 printf（经纬度用整数微度传，见开发文档 5.6），所以**不要**在链接选项里加 `-u _printf_float`，省几 KB Flash。

### 8.2 建立自己的目录

在工程根目录下新建三个目录，之后写代码都放这里，**不要去改 `Core/` 里 CubeMX 生成的内容**（除了 `USER CODE` 块）：

```
fallguard/
├─ BSP/          bsp_led.c/h  bsp_key.c/h  bsp_delay.c/h  bsp_oled.c/h
│                bsp_mpu6050.c/h  bsp_gps.c/h  bsp_esp.c/h  bsp_jq8900.c/h  bsp_rtc.c/h
├─ App/          app_main.c/h  app_fall.c/h  app_report.c/h  app_ui.c/h  app_json.c/h
└─ ThirdParty/   lwgps/  所选 OLED 驱动配套字库
```

### 8.3 把已完成模块加进 CMakeLists.txt

根 `CMakeLists.txt` 是用户维护入口，当前文件声明仅首次生成、允许修改；不要求 USER CODE 标记。`cmake/stm32cubemx/` 保持生成器管理。每完成一个模块，只添加实际存在的源码，避免递归搜入第三方例程、测试或其他芯片代码。

例如 C01/C02 已创建 `BSP/bsp_led.c` 后，在根文件现有 target 区域添加：

```cmake
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    "${CMAKE_SOURCE_DIR}/BSP/bsp_led.c"
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    "${CMAKE_SOURCE_DIR}/BSP"
    "${CMAKE_SOURCE_DIR}/App"
)
```

后续按 C01～C10 逐块添加 App/BSP 源文件。lwgps 须固定版本并添加其需要的头文件和选项文件；若保留上游布局，将实际 `.../lwgps/src/include` 加入 include 路径，而不是笼统添加一个 `ThirdParty/lwgps` 就认为完成移植。

根文件已有 `CMAKE_EXPORT_COMPILE_COMMANDS TRUE`，配置后 `build/Debug/compile_commands.json` 可供编辑器索引。

### 8.4 验证能否编译

确保 CMake、Ninja、arm-none-eabi-gcc 可执行文件可被终端或 STM32 插件环境找到。现有 `CMakePresets.json` 已引用 `cmake/gcc-arm-none-eabi.cmake`，在工程根目录执行：

```powershell
cmake --preset Debug
cmake --build --preset Debug
```

成功产物应为 `build/Debug/fallguard.elf`，不能仅以出现目录作为编译成功。若不用 preset，需要显式指定工具链：

```powershell
cmake -S . -B build/Debug -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build build/Debug
```

在已连接 ST-Link 且准备烧录时，可使用：

```powershell
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg -c "program build/Debug/fallguard.elf verify reset exit"
```

---

## 9. 上电验证清单

生成完先别急着写业务代码，按这个清单逐项确认硬件没问题（对应开发文档的 C01、C03）：

| #   | 验证项                | 方法                                                         | 通过标准                 |
| --- | ------------------ | ---------------------------------------------------------- | -------------------- |
| 1   | 时钟 72 MHz          | 在 main 的 USER CODE 块打印 `(unsigned long)HAL_RCC_GetHCLKFreq()`，格式为 `%lu`   | 打印 72000000          |
| 2   | 调试串口               | `printf("hello\r\n")`                                      | 串口助手 115200 收到       |
| 3   | LED                | 在主循环按 tick 每 500 ms 翻转 LED | 每秒闪一次                |
| 4   | 按键                 | 循环打印 `HAL_GPIO_ReadPin(KEY1_GPIO_Port, KEY1_Pin)`                 | 不按=1，按下=0            |
| 5   | **LSE 起振** ⚠️      | 通过 HAL/BSP 读取 RTC 时间，按 tick 间隔约 1 秒再次读取                                     | 值增加了约 1              |
| 6   | **PB4 能当 GPIO** ⚠️ | 把 PB4 拉高拉低，万用表量                                            | 电平跟着变（不变就是 JTAG 没关掉） |
| 7   | 软件 I2C 波形          | 移植 OLED 驱动后点亮屏幕                                            | 屏幕出字                 |
| 8   | I2C1 通             | 读 MPU6050 的 `WHO_AM_I`(0x75)                               | 返回 0x68              |
| 9   | GPS 有数据            | USART2 中断放环形缓冲，主循环限量转发原始 NMEA                                 | 看到完整的 `$GNGGA` / `$GPGGA` 等 NMEA 句      |

---

## 10. 常见问题排查

| 现象 | 原因 | 解决 |
|---|---|---|
| **OLED 完全不亮** | PB4 还是 JTAG 的 NJTRST | 检查 SYS → Debug 是否选了 **Serial Wire**；生成后在 `gpio.c` 或 `HAL_MspInit` 里应该能看到 `__HAL_AFIO_REMAP_SWJ_NOJTAG()` |
| **RTC 不走 / 初始化进 Error_Handler** | ① LSE 没起振 ② 备份域残留了旧的 RTC 配置 | ① 实测 LSE 起振并确认 RCC 返回值 ② 检查备份域时钟源残留；确需清备份域时先开启备份域写访问，复位后重新配置 RTC 的 LSE 时钟选择，再初始化 RTC。复位会清除备份域配置，不把单独两条复位宏永久放在 MX_RTC_Init 前 |
| **RTC 时间乱跳** | 分频不对 | 确认时钟树里 RTC Source = **LSE**，CubeMX 会自动配 32767 分频 |
| **串口收到乱码** | 波特率不匹配 | USART1/3 必须 115200，USART2 必须 9600，串口助手也要对上 |
| **`printf` 没输出** | 缓冲没关 / 没重定向 | 确认加了 `setvbuf(stdout, NULL, _IONBF, 0)` 和 `_write` |
| **编译报 `_write` 重定义** | 和 syscalls 冲突 | 保留一份 strong _write；生成的 weak _write 可由其覆盖。或只实现 __io_putchar。两份 strong 定义才需删去重复，不能靠再加 weak 声明保证选择正确实现 |
| **编译报 `LED_Pin` / `KEY1_Pin` 未定义** | 忘了设 User Label | 回到 System Core → GPIO 面板，最右列 User Label 填上第 4.0 节的标签，重新 Generate Code |
| **OLED 偶尔花屏 / 读不到 ACK** | 上拉太弱，SDA 上升沿太慢 | 量 SCL/SDA 对 VCC 的电阻，没有 4.7 kΩ 就自己加（见 4.3） |
| **CubeMX 没有 CMake 选项** | 版本旧 | 使用支持 CMake 生成的版本打开现有 fallguard.ioc；8.3 不是转换教程 |
| **重新生成代码后自己的代码丢了** | 写到了 `USER CODE` 块外面 | Core C/H 只在成对 USER CODE 块内手改；外设配置经 CubeMX 重新生成，根 CMakeLists 由用户维护 |

---

## 11. 配完对照表（自检）

生成后回来对一遍，全部对上就说明配好了：

| 引脚          | 功能           | **User Label**          | 外设     | 模式                       |
| ----------- | ------------ | ----------------------- | ------ | ------------------------ |
| PA2 / PA3   | GPS          | —                       | USART2 | Async 9600               |
| PA8         | LED          | `LED`                   | GPIO   | Out PP, Low, 初始 High     |
| PA9 / PA10  | 调试           | —                       | USART1 | Async 115200             |
| PB0         | JQ8900 SDA   | `JQ_SDA`                | GPIO   | Out PP, Low, 初始 High     |
| PB1         | JQ8900 BUSY  | `JQ_BUSY`               | GPIO   | Input PU（可选）             |
| PB4 / PB5   | OLED SCL/SDA | `OLED_SCL` / `OLED_SDA` | GPIO   | **Out OD + 外部上拉至 3.3 V, Low**, 初始 High |
| PB6 / PB7   | MPU6050      | —                       | I2C1   | Fast Mode 400 kHz        |
| PB10 / PB11 | ESP-01S      | —                       | USART3 | Async 115200             |
| PB12        | ESP-01S EN   | `ESP_EN`                | GPIO   | Out PP, Low, 初始 High     |
| PC8 / PC9   | 按键 K1/K2     | `KEY1` / `KEY2`         | GPIO   | Input PU                 |
| PC14 / PC15 | RTC 晶振       | —                       | LSE    | —                        |
| PA0         | 不用           | —                       | —      | 保持默认                     |
