# C09 MQTT 与 JSON 回放

使用实际 `bsp_esp`、`bsp_rtc`、`app_time`、`app_json`、`app_report`，模拟 UART、RTC、GPS 和报警状态。先运行项目 CMake 配置，生成被 Git 忽略的两份本地配置；回放通过 `tests/time_sync/config.h` 注入假 broker/凭据，不连接真实服务器。

从根目录用桌面 C 编译器运行：

```sh
cc -std=c11 -Wall -Wextra -Werror -DTEST_MQTT -include tests/time_sync/config.h -Itests/time_sync -IBSP -IApp BSP/bsp_rtc.c BSP/bsp_esp.c App/app_time.c App/app_json.c App/app_report.c tests/mqtt/test_mqtt.c -o build/test_mqtt
./build/test_mqtt
```

覆盖：

- JSON 整数边界、固定命令语法、1/3600 秒边界、缺参数/重复键/未知键/多余尾部/无效数值拒绝。
- CONNECTED 端口带引号/纯数字两种格式与最终 OK 的两种顺序；错误主机/端口/LinkID/尾部拒绝；订阅完成前不能上报；只收 OK、未收匹配连接 URC 时超时。
- AT 选项和 JSON 转义的实际字节，发送缓冲在 TX 期间不被覆盖，发布错误不计成功。
- 单字节拆分、连续 URC、载荷内换行及 OK、NUL/超长载荷、错误 topic、命令 FIFO 满、截断载荷及 UART 错误。
- SNTP 拒绝后 MQTT 继续、未校时时 ts=0、周期边界、立即查询、重复 clear 不触发报警、fall/cancel 顺序。
- 断线后重新连接/订阅，只上报当前状态，在线修改的周期保留到 MCU 重启。

此回放不能代替 ESP-01S 实物 AT 应答、云端真实上报及本地任务并发验证。见 [C09验收.md](../../docs/C09验收.md)。
