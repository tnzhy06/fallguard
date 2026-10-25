# JQ8900 一线协议回放

使用模拟 TIM3 和 GPIO，编译实际 BSP 与语音控制器；模拟计时理想，不测量真实 IRQ 延迟或音频输出。

```sh
cc -std=c11 -Wall -Wextra -Werror -Itests/jq8900 -IBSP -IApp tests/jq8900/test_jq.c -o build/test_jq
./build/test_jq
```

覆盖 0x89 示例及全部 256 字节的 LSB 位序、1 ms/4 ms/500 µs/1500 µs 脉宽、字节间隔，手册选曲 2/15 与音量 20，五位数字边界，发送中消警，计时回绕、定时器失败，FLASH/停止/音量初始化、BUSY 重播与早期取消。
