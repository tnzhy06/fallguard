# 简单跌倒演示回放

在工程根目录用桌面 C 编译器编译真实判定模块，HAL/BSP 由固定样本与时钟代替：

```sh
cc -std=c11 -Wall -Wextra -Werror -Itests/mpu6050 -IBSP -IApp tests/fall_demo/test_fall.c App/app_fall.c -o build/test_fall
./build/test_fall
```

验证 60° 和 1000 ms 边界、正负 Roll/Pitch、短时倾斜复位、报警锁定、消警后需两轴恢复、手动测试、传感器错误、重复/中断采样与计时回绕。只验证演示逻辑，不代表真实跌倒检测精度。
