# leech001/MPU6050 移植记录

来源：[leech001/MPU6050](https://github.com/leech001/MPU6050)，固定提交 [37856b26bedd32e04d4cead2d6c028125d4a3ba7](https://github.com/leech001/MPU6050/commit/37856b26bedd32e04d4cead2d6c028125d4a3ba7)。原文件为 Src/mpu6050.c 和 Src/mpu6050.h；保留 Bulanov Konstantin 的作者、GPL-3.0-or-later 及 TKJElectronics/KalmanFilter 来源声明。LICENSE 与该提交一致。

mpu6050.c/h 是本项目修改后的移植版本，2026-10-25：

- MPU6050_Init → mpu6050_configure：保留句柄参数及 WHO_AM_I 检查；检查每次 HAL 返回值。使用 F1 HAL。范围沿用 ±2 g、±250°/s；显式设置 DLPF_CFG=3、SMPLRT_DIV=9，得到 100 Hz；读回四个范围/滤波/分频寄存器确认后使用对应系数。INT_ENABLE=1，但仅通过 I2C 轮询 INT_STATUS，不占用 MCU 中断引脚。
- MPU6050_Read_All → mpu6050_read_all：保留 14 字节连读、带符号解码、g/dps/温度换算。增加数据就绪检查及返回状态，失败不提交数据；未就绪不重复参与滤波。去掉未使用的分轴读取函数。每次 HAL 请求超时参数从 100 ms 缩为 5 ms；HAL 内部等待和实际总耗时仍需上板测量。
- Z 轴改用与 X/Y 相同的 16384 LSB/g，删除上游本机专用的 14418.0，不冒充已完成加速度标定。
- Read_All 中的双轴姿态解算 → mpu6050_filter_update/reset；Kalman_getAngle → 模块内部 mpu6050_kalman_angle。保留上游协方差、偏置与角度更新公式及 Q_angle=0.001、Q_bias=0.003、R_measure=0.03。状态改为 static，并为每次初始化清零；首样本用重力角初始化，后续按真实 tick 差积分。
- 使用 g 单位的 double 先平方，避免原始 int16 值平方求和溢出；roll 使用 atan2(ay,sqrt(ax²+az²))，修复 X/Z 为零、Y 非零时上游输出 0°的问题。保留 ±180° pitch 跨界处理；X 轴解算所需符号翻转仅作用于局部滤波输入，避免改写实际陀螺仪读数。
- 复位写入统一由 mpu6050_reset 执行；通信失败时记录操作 R/W、寄存器、HAL 状态、HAL_I2C_GetError 和当时 BUSY 标志。ID 与配置读回不符分别打印实际值/期望值，避免只给出一条混合错误。日志仅在主循环错误路径输出，不增加重试或修改总线参数。
- 修复原头文件卫哨提前结束的问题。BSP 封装启动等待、200 样本陀螺仪均值零偏校准、时间片与整数单位输出；App 不包含本目录头文件。

寄存器依据：[InvenSense MPU-6000/MPU-6050 Register Map，RM-MPU-6000A-00 Rev 4.2](https://invensense.tdk.com/wp-content/uploads/2015/02/MPU-6000-Register-Map1.pdf)，重点为 4.2～4.5、4.15～4.20 和电源管理章节。DLPF=3 对应加速度约 44 Hz、陀螺仪约 42 Hz 带宽；1 kHz / (1+9) = 100 Hz。原厂旧链接当前可能重定向到资料搜索页。

限制：此双轴 Kalman 解算沿用上游 restricted-roll 模型，不输出航向，不把剧烈运动下的重力估计当成准确身体倾角。腰带安装参考轴、加速度六面标定与最终量程仍需实际设备确认。

本次重新接入仅用于六轴及 Roll/Pitch 实时显示，复用已审查移植与现有 BSP；已通过连接器核对 upstream master 仍指向上述固定提交，保留 LICENSE 原文。驱动诊断前缀统一为 [MPU]，App 在一次快照上分时输出三行，OLED 约每 100 ms 刷新；不恢复 C06 采集缓冲、相对身体倾角或自动跌倒状态机。

调试过滤：错误打印通过项目 BSP/debug_config.h 的 DEBUG_PRINT/DEBUG_LOG_ERRORS 及总开关控制；仅改变诊断输出，不改变 I2C 返回值、错误状态或姿态计算。
