# MaJerle/lwgps 移植记录

来源：[MaJerle/lwgps](https://github.com/MaJerle/lwgps)，固定提交 [4b148bd687b626a63f6e0ce6acff2e2d8bcbc4fa](https://github.com/MaJerle/lwgps/commit/4b148bd687b626a63f6e0ce6acff2e2d8bcbc4fa)，git describe 为 v2.4.1-1-g4b148bd。保留 Tilen MAJERLE 等作者版权声明及完整 MIT LICENSE。

以下文件与固定提交逐字节一致，未修改第三方源码：

- 上游 lwgps/src/lwgps/lwgps.c → src/lwgps/lwgps.c。
- 上游 lwgps/src/include/lwgps/lwgps.h、lwgps_opt.h → src/include/lwgps/。
- 上游 LICENSE → LICENSE。

项目新增 lwgps_opts.h，启用 double、状态回调、校验和、GGA/RMC/GSA/GSV 与卫星详情，关闭 PUBX、距离/方位计算。double 用于保持经纬度微度换算精度，不启用浮点 printf。库使用调用方提供的静态句柄，无 malloc。上游会解析 RMC 的速度字段，项目 BSP 不输出或使用速度，也不要求该字段非空。

根 CMakeLists.txt 添加源文件及 include 路径，不修改生成的 cmake/stm32cubemx/。BSP/bsp_gps.c 封装接收环形缓冲、完整帧和字段检查、校验状态与数据新鲜度。

- lwgps 已将度分格式转换成十进制度，BSP 仅转换为有符号整数微度。
- GGA 提供经纬度、海平面海拔及卫星数；RMC 提供有效状态和 UTC 日期时间；GSA 提供 HDOP（该固定版本未解析 GGA 的 HDOP）。
- GGA 质量 1/2/3、RMC=A 且各自未过期时置 fix=1；用户确认 5 秒过期。无效或过期后保留历史坐标、海拔，首次定位前 has_position=0。
- GSV 用原库的多星座详情，BSP 验证分句序号、完整性和字段范围。完整 GP/BD 等连续组在随后的非 GSV 句子到达时发布；合计最多 48 项。空信号与数值 00 区分，统计已报告数值的卫星数及最大 C/N₀。信号单位依据模块使用手册第 9 页，dBHz。
- 用户确认最大 C/N₀ 四档：无＝无效/过期/0，弱＝1～29，中＝30～39，强＝40～99；5 秒失效。信号可在 fix=0 时有效，不作为定位成功条件。

新增字段仅用于 BSP、OLED 和串口诊断，未修改 MQTT topic 或 JSON 字段。
