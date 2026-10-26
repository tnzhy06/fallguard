# MPU6050 驱动与 Kalman 回放

用模拟 HAL 寄存器编译真实 BSP 与移植源文件，不访问硬件。在工程根目录用桌面 C 编译器执行：

```sh
cc -std=c11 -Wall -Wextra -Werror -Itests/mpu6050 -IBSP -IThirdParty/leech001_mpu6050 tests/mpu6050/test_mpu.c BSP/bsp_mpu6050.c ThirdParty/leech001_mpu6050/mpu6050.c -lm -o build/test_mpu
./build/test_mpu
```

覆盖 ID/配置失败、带符号换算、首次姿态、Roll ±90°、混合姿态、Pitch 跨界、tick 回绕、200 样本陀螺零偏、重复样本与传感器错误。实际精度和调度间隔仍需上板检查。

串口快照调度回放：

```sh
cc -std=c11 -Wall -Wextra -Werror -Itests/mpu6050 -IBSP -IApp tests/mpu6050/test_output.c App/app_fall.c -o build/test_mpu_output
./build/test_mpu_output
```

验证三行 A/G/Roll/Pitch 对应同一采样号、持续 GPS 轮询、传感器错误后停止输出旧姿态。

调试输出测试读取 BSP/debug_config.h，可用不同 include 路径中的配置副本验证组合；本轮覆盖默认、仅角度、仅 GPS UTC、全静默及全部开启。
