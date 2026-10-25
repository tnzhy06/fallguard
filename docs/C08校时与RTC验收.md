# C08 第一部分：Wi-Fi 校时与 RTC

2026-10-26，用户确认 C07 验收通过，已推送 [`a1820b6`](https://github.com/tnzhy06/fallguard/commit/a1820b674f341940b890a6892bf56100813ea000)。C08 的 Wi-Fi、SNTP 和 RTC 代码已编译及软件回放通过。用户在三服务器变更后确认“现在可以了”，本轮按已通过校时验证提交并进入 C09；VBAT 独立持时尚无单独测试记录。

## 先填写 Wi-Fi

修改仅保存在本机的 `BSP/esp_config_local.h`：

```c
#define ESP_WIFI_SSID     "你的WiFi名称"
#define ESP_WIFI_PASSWORD "你的WiFi密码"
```

此文件在 CMake 配置时从 [esp_config.example.h](../BSP/esp_config.example.h) 自动创建，已有文件不会覆盖，并已加入 `.gitignore`。填写后重新编译 Debug、烧录 [fallguard.elf](../build/Debug/fallguard.elf)。空名称时仍会探测 AT、关闭回显、读取 GMR，之后停在 SET WIFI，不主动连接。

SSID 最多 32 字节，密码最多 64 字节，不接受控制字符；代码会对 AT 参数中的逗号、双引号、反斜杠转义。写入 C 字符串本身时，双引号和反斜杠仍需按 C 语法转义。调试输出隐藏连接命令中的凭据。

## 接线与指令

引脚与 CubeMX 配置不变：PB10（USART3 TX）接 ESP RX，PB11（USART3 RX）接 ESP TX，PB12 接 EN，共地；串口为 115200 8N1。ESP-01S 继续使用已刷入的 1471 号固件，本轮不重刷，不从编号推断 AT/SDK 版本。

以 [ESP-AT User Guide.pdf](ESP-AT%20User%20Guide.pdf) 为依据：Wi-Fi 见物理第 60～63 页，SNTP 见物理第 114～116 页。依次发送：

```text
AT
ATE0
AT+GMR
AT+CWMODE=1
AT+CWJAP="配置的名称","配置的密码"
AT+CIPSNTPCFG=1,8,"ntp.aliyun.com","ntp.sjtu.edu.cn","cn.ntp.org.cn"
AT+CIPSNTPTIME?
```

每条实际命令以 CR-LF 结束，等待上一条最终响应后再发下一条。USART3 接收使用单字节中断和 1024 字节环形缓冲；主循环分批解析完整行，发送用中断方式，不在中断里打印、解析或等待网络，不使用 DMA。

`AT+GMR` 的版本行打印为 `[ESP GMR] ...`，请在实测时保存原始版本内容。用户已提供真实响应，版本记录见下方。若命令返回 ERROR，显示 AT REJECT 并保留 RTC，不擅自改为其他 AT 方言；需要根据实际日志核对手册差异。

## 校时与 RTC 行为

SNTP 配置的 `OK` 仅表示配置成功。查询返回 `+CIPSNTPTIME:Www Mmm dd hh:mm:ss yyyy` 后，还须收到该事务的最终 `OK`，才向 RTC 应用时间。支持个位日期的空格填充、英文月份；检查完整日期、闰年和时分秒，拒绝 1970 等未同步时间、越界日期及多余尾部数据。

- RTC 保存东八区本地日历，ESP 已按时区 8 返回本地时间，代码不再加 8 小时。
- RTC 数据同时给出 Unix **UTC 秒**，转换时扣除 8 小时并处理跨日；未有效校时为 `valid=0, ts=0`。
- 初次校时后，断网或查询失败不覆盖已有有效 RTC。持续时间和跌倒计时仍使用 HAL tick，校时跳变不改变报警规则。
- 日期支持 2000～2099 年；没有有效校时标记时，不将 CubeMX 的 2000-01-01 默认日期作为真实时间。
- GPS UTC 保持原有解析和显示，不加时区、不写 RTC。

STM32F1 的 HAL 日历日期保存在 RAM，`HAL_RTC_GetTime()` 还会在跨日时回写计数器。因此本实现使用 BSP 保存的“校时时本地午夜对应的 UTC 秒基准 + RTC 硬件累计秒”恢复完整日历，不把裸计数器当 Unix 时间。BKP DR1～DR4 保存格式标记、32 位基准和校验；校时成功后才提交标记。没有每天写 Flash 或备份寄存器的操作。

`Core/Src/rtc.c` 的 USER CODE 检查块在备份有效时跳过默认日期时间设置。所有后续 RTC 读写走 `bsp_rtc`，不要另行调用 HAL 的 GetTime/SetDate 或修改计数器，否则会破坏这个累计秒基准。

普通 MCU 复位时应能恢复时间；全断电后能否保留取决于 VBAT/备份域是否仍供电。没有后备供电时，完全断电丢时是预期行为，需要重新联网校时。软件回放不能证明实物 VBAT 和 LSE 精度。

## 本轮等待参数

| 操作 | 当前值 |
|---|---|
| 上电或识别到 ready 后等待 | 2 秒 |
| 普通 AT 命令响应超时 | 2 秒 |
| Wi-Fi 连接响应超时 | 20 秒 |
| 连接失败/命令超时/接收失步后恢复 | 等 30 秒，EN 拉低 100 ms，再重新启动流程 |
| SNTP 查询间隔/单轮获取窗口 | 2 秒/30 秒 |
| 有响应但一直无有效时间 | 结束本轮，60 秒后重试 |
| 有效校时后的周期复校 | 1 小时 |
| RTC 快照刷新/调试输出 | 200 ms/默认 1 秒 |

这些是本项目的初始调度参数，尚未据 1471 实物响应标定。等待期间本地功能继续运行。启动等待阶段的接收噪声会丢弃并记录；正常事务中的 UART 错误、环形缓冲溢出或超长行会放弃当前事务并重建会话，避免用破损响应误校时。持续无响应会走连接恢复；明确不支持的指令停止自动尝试，等待检查。

## 显示与日志

默认显示新增 RTC 页面，K1 在原 6 页和 RTC 页之间循环；MPU 六轴、Roll/Pitch、GPS 信息及报警覆盖页保留。RTC 页显示日期、时分秒、UTC 时间戳及 Wi-Fi 状态：

| 显示 | 含义 |
|---|---|
| TIME UNSET ts:0 | 没有有效校时或可恢复的备份 |
| SET WIFI | 本地 Wi-Fi 名称未填写或配置不合法 |
| JOIN / SNTP CFG / SNTP WAIT | 正在连接/配置/查询 |
| WiFi:1 UP | Wi-Fi 流程已成功；当前是否已校时看日期与 ts |
| WiFi:0 RETRY | 连接恢复等待中，已有 RTC 仍可走时 |
| AT REJECT | 指令被拒绝，请保存版本与响应日志 |
| RTC ERROR | RTC 读写、时钟或日期范围异常 |

UP 不表示 MQTT 已连接。LED 仍沿用原来的“网络未运行”常亮/报警快闪模式，本轮不接入 MQTT，不改变 topic/JSON 格式。

[debug_config.h](../BSP/debug_config.h) 新增 `DEBUG_LOG_ESP=1`（命令步骤和版本）、`DEBUG_LOG_ESP_RX=1`（原始响应行）、`DEBUG_LOG_TIME=1`（校时及 RTC 周期输出），`DEBUG_TIME_PERIOD_MS=1000`。当前其他模块日志及 `DEBUG_LOG_ERRORS` 均为 0；ESP/RTC 自身错误分别随 ESP/TIME 分类保留，也可由通用错误开关开启。当前已仅保留本轮校时相关输出；总开关关闭不停止校时、RTC 或本地检测。

## 上板验收

1. 填好本地 Wi-Fi，编译并烧录。观察 GMR 版本和实际响应；首次无有效备份时应先 TIME UNSET，不能直接出现假日期。
2. 联网成功后，串口出现 `[RTC] SNTP applied, local=UTC+8`；OLED 时间应与当前北京时间接近，Unix 秒对应 UTC。精度按秒观察，本轮不做晶振频率校准。
3. 关闭 Wi-Fi，确认日期时间继续每秒变化，GPS、MPU、K1、K2 报警和 JQ8900 仍正常；恢复 Wi-Fi 后观察重连和后续校时。
4. 在已有有效校时后按 MCU 复位，确认出现 `restored=1` 且日期时间继续；再断开主电源测试 VBAT 保持，按实际板上后备供电情况判定。
5. 检查 `dt/max` 和 GPS 错误/丢字节计数，确认发 AT 与播放语音期间本地任务仍可接受。日志本身的串口发送会占用少量主循环时间，可通过分类开关对比。
6. 若卡在 AT REJECT 或超时，保留 GMR 和 ESP_RX 日志；不能据本地模拟器通过就认定真实 1471 固件支持所有指令。

## 文件与验证状态

新增 [bsp_esp.c](../BSP/bsp_esp.c)/[.h](../BSP/bsp_esp.h)、[bsp_rtc.c](../BSP/bsp_rtc.c)/[.h](../BSP/bsp_rtc.h)、[app_time.c](../App/app_time.c)/[.h](../App/app_time.h) 与 Wi-Fi 配置示例。修改主调度、OLED、日志、根 CMake 和 Core 的 USER CODE 接入；未改物理引脚、`.ioc`、HAL 或生成的构建文件。

Debug 编译成功，RAM **7728 B**、FLASH **68424 B**，原 `build/Debug/compile_commands.json` 保留。修改源码的严格警告检查通过；[RTC/ESP/校时回放](../tests/time_sync/README.md)、默认/静默/全开日志下的实际调度器回放，以及实际 OLED 帧缓冲检查通过。

已收到真实 ESP-01S 联网及 SNTP 查询日志，联网成功但时间尚未同步；三服务器变更、有效 SNTP 校时、复位/VBAT 持时和并发运行仍待验收。C09 MQTT/JSON 与网络上报未实现。

## 首次实测记录与 SNTP 排查

2026-10-26 用户提供的 GMR 原始版本内容：

```text
AT version:2.3.0.0-dev(s-bcd64d2 - ESP8266 - Jun 23 2021 11:42:05)
SDK version:v3.4-22-g967752e2
compile time(b498b58):Jun 30 2021 11:28:20
Bin version:2.2.0(ESP8266_1MB)
```

日志已确认 AT、ATE0、GMR、CWMODE、CWJAP 和单服务器 CIPSNTPCFG 指令收到 OK，Wi-Fi 出现 CONNECTED / GOT IP。CIPSNTPTIME 查询持续返回 `Thu Jan  1 08:01:xx 1970`：ESP 尚未取得有效网络时间，RTC 拒绝此日期属于预期行为，尚不能判断 RTC 硬件校时/持时通过。

原代码仅指定 cn.ntp.org.cn；为排除单服务器可达性问题，本轮改为配置 3 个服务器：ntp.aliyun.com、ntp.sjtu.edu.cn、cn.ntp.org.cn。三服务器语法仍来自随附 ESP-AT PDF 第 114～116 页；ntp.aliyun.com 作为公网 NTP 地址由[阿里云官方文档](https://help.aliyun.com/en/ecs/user-guide/alibaba-cloud-ntp-server/)确认，另可对照[ESP8266 v2.2 官方指令说明](https://docs.espressif.com/projects/esp-at/en/release-v2.2.0.0_esp8266/AT_Command_Set/TCP-IP_AT_Commands.html#at-cipsntpcfg-query-set-the-time-zone-and-the-sntp-server)。不据此宣称服务器从用户网络可达或已校时成功。

重新烧录后确认 TX 中为三服务器配置，等到真实日期与 `[RTC] SNTP applied`。如果仍持续返回 1970，可改用能上网的手机 2.4 GHz 热点交叉测试；若热点成功，优先排查原网络的 DNS 和 UDP 123/NTP 放行情况。仅连接 AP 并获得 IP 不等于已能访问互联网或 NTP 服务。阿里云文档也提示同步失败时检查 UDP 123 是否被阻止。

阶段推进：用户确认三服务器配置后校时可用，要求提交 GitHub 并根据本机阿里云部署资料进入 MQTT 接入。未据此补写未提供的 VBAT、晶振精度或 MQTT 实测结论。
