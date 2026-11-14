# leech001/MPU6050 的 STM32F1 INT 移植

来源：[leech001/MPU6050](https://github.com/leech001/MPU6050)，固定提交 [37856b26bedd32e04d4cead2d6c028125d4a3ba7](https://github.com/leech001/MPU6050/commit/37856b26bedd32e04d4cead2d6c028125d4a3ba7)。原文件为 Src/mpu6050.c 和 Src/mpu6050.h；保留 Bulanov Konstantin 的作者、GPL-3.0-or-later 及 TKJElectronics/KalmanFilter 来源声明。LICENSE 与该提交一致。

2026-11-14 对照上述固定提交重新整理移植，接入 PA1 / EXTI1 数据就绪中断。DATA_RDY 接入、寄存器校验、故障恢复和校准由本工程补充。

- MPU6050_Init → mpu6050_configure：保留句柄参数及 WHO_AM_I 检查，检查每次 HAL 返回值。默认 ±2 g、±250°/s、DLPF_CFG=3、SMPLRT_DIV=9，对应 100 Hz。读回量程、滤波、分频和 INT 配置。INT_PIN_CFG=0x20，采用高电平有效、推挽、锁存至读取 INT_STATUS；配置时 INT_ENABLE=0。稳定等待完成后，mpu6050_enable_data_ready 清除旧 INT_STATUS，设置并读回 INT_ENABLE=1。
- MPU6050_Read_All 拆为 mpu6050_start_read 和 mpu6050_decode_frame：前者从 INT_STATUS / 0x3A 异步连读 15 字节，先检查 BUSY，避免进入 HAL 的总线忙轮询；HAL_OK 仅表示成功启动。后者由主循环在完成通知后执行，保留带符号解码和 g/dps/温度换算，返回 1 表示有效新数据，0 表示没有 DATA_RDY 且保持调用者数据不变。持久接收缓冲区、事务状态和超时由 BSP 管理。
- Z 轴改用与 X/Y 相同的 16384 LSB/g，删除上游本机专用的 14418.0，不冒充已完成加速度标定。
- Read_All 中的双轴姿态解算 → mpu6050_filter_update/reset；Kalman_getAngle → 模块内部 mpu6050_kalman_angle。保留上游协方差、偏置与角度更新公式及 Q_angle=0.001、Q_bias=0.003、R_measure=0.03。状态改为 static，并为每次初始化清零；首样本用重力角初始化，后续按真实 tick 差积分。
- 使用 g 单位的 double 先平方，避免原始 int16 值平方求和溢出；roll 使用 atan2(ay,sqrt(ax²+az²))，修复 X/Z 为零、Y 非零时上游输出 0°的问题。保留 ±180° pitch 跨界处理；X 轴解算所需符号翻转仅作用于局部滤波输入，避免改写实际陀螺仪读数。
- 复位写入统一由 mpu6050_reset 执行；通信失败时记录操作 R/W、寄存器、HAL 状态、HAL_I2C_GetError 和当时 BUSY 标志。ID 与配置读回不符分别打印实际值/期望值，避免只给出一条混合错误。日志仅在主循环错误路径输出，不增加重试或修改总线参数。
- 修复原头文件卫哨提前结束的问题。BSP 封装启动等待、默认 200 个有效中断样本的陀螺仪零偏校准、INT 通知和整数单位输出。采样时间戳采用 EXTI 回调记录的 HAL tick，滤波按该时间差更新。

寄存器依据为 InvenSense RM-MPU-6000A-00 Rev 4.2，重点为 4.2～4.5、4.14～4.19 和电源管理章节。DLPF=3 对应加速度约 44 Hz、陀螺仪约 42 Hz 带宽；1 kHz / (1+9) = 100 Hz。原厂旧链接已重定向，以下镜像保留原厂文档内容。[寄存器手册](https://hw101.tbs1.de/mpu6050/doc/MPU-6000-6050-Register-Map.pdf)

限制：此双轴 Kalman 解算沿用上游 restricted-roll 模型，不输出航向，不把剧烈运动下的重力估计当成准确身体倾角。腰带安装参考轴、加速度六面标定与最终量程仍需实际设备确认。

## INT 接线与处理

INT → PA1，SCL → PB6，SDA → PB7，AD0 默认接地。PA1 使用上升沿中断和内部下拉；优先级由 `fallguard.ioc` 管理。调用路径为 `EXTI1_IRQHandler → HAL_GPIO_EXTI_IRQHandler → HAL_GPIO_EXTI_Callback → BSP_MPU6050_DataReadyIRQ`。EXTI 回调只记录事件序号和时间戳，主循环启动 `HAL_I2C_Mem_Read_IT`，I2C1 event/error 中断推进事务。两个 I2C 中断均为抢占优先级 0、子优先级 0。

`HAL_I2C_MemRxCpltCallback` 和 `HAL_I2C_ErrorCallback` 按句柄路由到 BSP，仅发布完成或错误通知。主循环完成解析、校准、滤波和日志；采样缓冲区在传输和解析结束前不复用。初始化寄存器读写仍使用有限超时的轮询接口，且只在采样事务停止时执行。

主循环没有待处理中断时直接返回。读取期间再次到达的中断保留到下一轮，一轮合并已积累的通知并读取当前一帧。锁存期间多次数据更新会覆盖旧帧。`bsp_mpu6050_sample_t.irq_count` 记录已处理的中断序号，开启 `DEBUG_LOG_MPU_ACCEL` 后可观察 `irq=` 日志。

默认 250 ms 没有有效中断样本或 I2C 失败时进入故障状态，2 秒后释放总线、重新初始化、使能 DATA_RDY 并校准。地址、分频、DLPF、Kalman 参数、校准数量和恢复超时统一在 `App/app_config.h` 管理。恢复后需保持设备静止。

单次异步读取以及等待空闲总线的期限由 `BSP_MPU6050_READ_TIMEOUT_MS` 管理，默认 5 ms。忙返回不消耗通知，不会不断重新计时；事务使用启动时保存的 INT 序号和时间戳。完成时记录实际完成 tick，主循环稍后处理不会误报事务超时。错误或超时后先关闭 I2C 外设事件/缓冲/错误中断、关闭外设并清理 NVIC 挂起状态，再等待恢复，避免旧事务继续写入缓冲区。回调在事务停止后被忽略。

`BSP_MPU6050_GetStats()` 提供采样启动数、完成数、错误数、超时数、忙重试次数、合并通知数、最长已完成读取耗时和最近错误码。自动恢复保留这些统计；显式调用 `BSP_MPU6050_Init()` 才清零。通知合并数不等同于实际丢帧数，锁存期间覆盖的帧无法仅靠 EXTI 计数得知。当前无 FIFO，不能保证在长时间阻塞后补齐历史帧。

## 验证

修改驱动后，在开发板上检查数据就绪中断、异步读取、静置校准，以及断开并恢复传感器后的自动恢复；通过串口日志和统计接口观察错误、超时及恢复情况。

板上连接 INT → PA1，上电静置完成校准后观察样本和 `irq_count` 增加。断开 INT 后应在配置的超时时间内显示传感器故障，重新连接并静置后应自动恢复。实际中断频率和并发负载下的采样间隔需通过开发板测量。

调试开关由 `App/app_config.h` 的 `DEBUG_OUTPUT_ENABLE` 和 `DEBUG_LOG_ERRORS` 管理，`BSP/bsp_log.h` 提供 `DEBUG_PRINT` 日志接口。
