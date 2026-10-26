# Fallguard 网站 Agent 提示词（简易展示版）

配套：[网站开发文档](网站开发文档.md)、[MQTT 课程演示方案](MQTT课程演示方案.md)。本版替代此前要求登录、SQLite、多页面和 SSE 的提示词。

## 可直接复制的开发提示词

```text
请开发 Fallguard 的简易课程展示网页。网站不是项目重点，目标是基本展示与三个已有 MQTT 操作，代码越简单、越容易启动和讲解越好。

先读根 AGENTS.md、docs/MQTT课程演示方案.md、docs/网站开发文档.md，检查现有 web/ 和 Git 改动，不覆盖用户工作。当前固件已实现 C09，用户已确认 MQTT 阶段通过；不要恢复旧 C09 备份、修改固件或重部署 EMQX。

只用普通 HTML + CSS + JavaScript，Python FastAPI + paho-mqtt + python-dotenv。后端提供网页，页面每秒 GET 一次状态。不要使用 Vue/React、npm/Vite、数据库、SSE、WebSocket、登录、账号、权限管理、多页面路由、历史查询、轨迹或报表。不做内置模拟系统，用 MQTTX 手动注入 JSON 联调。

文件放在 web/：main.py、requirements.txt、.env.example、README.md、static/index.html、static/style.css、static/app.js。真实 .env 加入 Git 忽略。默认只展示配置中的 belt01，需要换设备时改 DEVICE_ID，不开发设备管理。

已确认 Broker 为 114.55.210.224:1883，当前账号密码从本地 .env 读取，不猜测旧密码，不写入前端。MQTT 3.1.1、QoS 0、Retain=false、Clean Session=true、Keep Alive=120。网站 Client ID 为 fallguard-web-server，不能与 belt01 或 MQTTX 重复。

订阅 fallguard/{DEVICE_ID}/loc 和 fallguard/{DEVICE_ID}/alarm，发布到 fallguard/{DEVICE_ID}/cmd。不订阅 cmd，不改主题和字段。

消息格式：
位置：{"id":"belt01","ts":0,"lat":22615755,"lon":114141317,"fix":1,"st":"ok"}
报警：{"id":"belt01","ts":0,"ev":"fall","lat":22615755,"lon":114141317,"fix":1}
消警：{"id":"belt01","ts":0,"ev":"cancel"}
下行只有 {"cmd":"loc"}、{"cmd":"clear"}、{"cmd":"intv","v":5}，周期整数 1～3600 秒，默认约定 5 秒。不附加命令 ID 或 ACK 字段。MQTT 未上报卫星数，不要求 sat 字段、不展示或推算卫星数。

一个页面展示：
- 设备 ID、网站 MQTT 连接状态、最近收到数据的时间。
- 报警状态及最近 fall/cancel 事件提示。
- 经纬度、定位有效性、设备时间与接收时间。
- 简单地图单点展示，底图失败时保留坐标与按钮。
- 查询位置、远程消警、修改周期三个操作。
- 内存中最近 20 条消息，后端重启即清空。

显示必须真实：
- 经纬度整数微度除以 1000000；Leaflet 使用 [纬度,经度] 和 WGS84 底图，保留地图署名。
- fix=0 不冒充有效位置，历史位置有标记，未定位 (0,0) 不落点。
- ts=0 显示设备时间未校准，不显示 1970；接收时间照常显示。
- loc.st 是状态快照，fall/cancel 是最近事件，分别展示；无 loc 时不默认设备正常。
- “网站 MQTT 已连接”不等于设备在线，不按固定无上报秒数判离线。
- “已发送”不等于执行成功；周期输入/最近请求值不等于设备确认值。
- 不显示协议没有的卫星数、电量、GPS 信号强度、海拔、HDOP、姿态角，不用假数据填空。

后端只有 GET /、GET /api/state、POST /api/command，另提供静态文件。state 返回内存快照，command 只接受三类定义好的对象，目标设备由后端配置决定。参数错误 422，MQTT/订阅未就绪 503；不排队离线命令、不自动重发 clear，不自动重试 POST。

一个 Paho 客户端，Callback API VERSION2，loop_start；FastAPI lifespan 启停它，部署一个 worker。回调做短小校验与内存更新，共享状态用短临界区线程锁；接口复制快照后返回。deque 最多 20 条，不建数据库或复杂消息处理框架。

校验配置设备的 topic/id、UTF-8、JSON、重复键、类型和范围；拒绝 bool 冒充整数、非法坐标及非法周期。坏消息不能覆盖好状态或使程序退出。保留消息标记历史，不当新实时报警。Paho MessageInfo 跟踪发送进度，不能仅凭 publish 返回成功就声称完成，更不能声称设备执行成功。

页面轮询上一次请求完成后才安排下一次，每次有超时，避免叠加。页面或 MQTT 断开时给清晰提示、保留旧数据，不伪装实时。远程消警只使用一个简单确认框；请求期间按钮防止重复点击。不做复杂 UI 动画或弹窗。

默认本机运行：从 web/ 执行 uvicorn main:app --host 127.0.0.1 --port 8000 --workers 1。没有登录，按本机课程展示使用，不自动开放公网接口。README 写 Windows/Ubuntu 的虚拟环境、安装依赖、配置和启动步骤。

按 W01 页面与骨架、W02 MQTT与展示、W03 三个操作与验收推进。进行 Python 语法和关键校验检查、真实浏览器基本验证，用 MQTTX 检查上下行。交付说明修改文件、启动方式及实际完成了哪些验证；MQTTX 注入不等于实物验收。本任务只开发网站，不改 App/BSP/Core/Drivers/.ioc/CMake，不部署或重置云端服务，用户未要求时不推送 Git。
```

## 简短验收提示词

```text
检查 Fallguard 网页是否满足简易展示版文档：单页、无登录、无数据库、无前端构建，默认本机运行。验证 MQTTX 注入的位置/报警/消警正确显示，三个命令精确发布，周期只接受整数 1～3600，断线提示与最近 20 条内存消息正常。检查 fix=0、ts=0、历史位置、设备在线未知、命令无 ACK 的显示没有造假。查看电脑/手机页面和地图失败态，确认静态文件没有 MQTT 密码。修复可复现问题并报告实际验证结果，不扩展管理功能，不改固件或服务器。
```
