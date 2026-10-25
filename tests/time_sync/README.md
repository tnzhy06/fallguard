# C08 校时与 RTC 回放

编译实际 `bsp_rtc.c`、`bsp_esp.c` 和 `app_time.c`，使用固定假 Wi-Fi 配置、模拟 UART/RTC/备份寄存器。先运行项目 CMake 配置，生成被 Git 忽略的 `BSP/esp_config_local.h`；回放通过 `config.h` 替换配置，不使用实际密码。

在具有桌面 C 编译器的环境中，从项目根目录执行：

```sh
cc -std=c11 -Wall -Wextra -Werror -include tests/time_sync/config.h -Itests/time_sync -IBSP -IApp BSP/bsp_rtc.c BSP/bsp_esp.c App/app_time.c tests/time_sync/test_time.c -o build/test_time
./build/test_time
cc -std=c11 -Wall -Wextra -Werror -DTEST_NO_WIFI -include tests/time_sync/config.h -Itests/time_sync -IBSP -IApp BSP/bsp_rtc.c BSP/bsp_esp.c App/app_time.c tests/time_sync/test_time.c -o build/test_time_empty
./build/test_time_empty
```

覆盖东八区到 UTC 秒的转换、闰年跨月、日期范围、模拟停机 40 天后恢复、备份损坏/RTC 写失败、AT/GMR/Wi-Fi/SNTP 指令顺序、特殊字符转义、无效日期/1970 时间拒绝、最终 OK 前不写 RTC、断网持时、SNTP 有限轮询/重试、空配置、超时复位、UART/环形缓冲/行缓冲丢失恢复、启动噪声和 tick 回绕。

回放不验证实物 1471 固件版本、网络连通性、LSE 精度、真实 VBAT 供电或中断延迟，硬件步骤见 [C08校时与RTC验收.md](../../docs/C08校时与RTC验收.md)。
