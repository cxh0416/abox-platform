# ABox 公共入网实现

内部资料，不作为业务对接协议对外交付。

`ABoxEnrollment` 持有申请、轮询、凭据校验和试连结果状态；产品提供 UID、VID、产品
hardwareContract、Boot/App 标识、HTTPS origin、就绪门禁和配置持久化适配。URL、请求体、
响应、requestId 和 pollToken 缓冲区均由产品持有。模块不写 Flash，也不持有产品配置 ABI。

`ABoxEnrollmentEc800Http` 执行 EC800 HTTPS POST，收到完整的 `QHTTPREAD: 0` 后回调入网
核心。`QHTTPSTOP` 只用于失败或取消时中止尚未完成的请求；中止失败会阻止新 HTTPS
操作，只有物理 modem reset 后才能调用 `AfterModemReset` 恢复。产品先确认 modem 上电、CA、
OTA 互斥和 AT 空闲，再调用 `Post`。HTTP 适配器先查询 PDP context 1，未激活时执行 QIACT，避免空配置启动依赖先建立过 MQTT。响应长度超过调用方缓冲区时拒绝解析。

入网核心保留 pending requestId/pollToken。202 使用合法 `retryAfterSec`，缺失或无效时
等待 120 秒。401/403/404/410 清除 request 凭据并等待 120 秒重新申请；409、网络和
服务端暂时错误保留阶段。试连失败保留 request，下一次优先 poll 重新领取凭据；领取
窗口关闭后再次申请。取消时等待 HTTP 或试连适配完成清理。

`ABoxEnrollmentMigration` 复用公共入网服务实现存量 PSK 迁移的请求关联、
15 秒响应确认等待和 10 分钟审批等待。产品仍负责本机安全准入、维护消息解析及
确认 202 响应的 MQTT PUBACK；只在关联响应确认且重新检查准入后启动 HTTPS。
审批等待超时仅在 HTTP 和试连空闲后取消，恢复原 MQTT；试连完成由产品报告结果。
未接入此能力的产品不能仅凭公共模块存在就声明支持。

MQTT 试连共用 `ABoxMqttTrial`。已有配置切换使用 `Start` 并等待旧连接响应 ACK；首次
入网使用 `StartFirst`，不需要旧配置或旧 ACK，120 秒试连超时、90 秒清理恢复超时。
`SAVED` 只能在 Flash 保存并读回后发送。首次试连恢复回调接收 `NULL` active config，
产品需清理候选连接、恢复入网前身份并保持普通 MQTT 关闭。恢复失败会阻止后续试连，
直到产品显式完成恢复。

主机测试覆盖请求复用、状态码、计时、响应边界、HTTP 清理门禁、首次试连以及恢复
阻塞。它们不证明蜂窝网络、Broker、Flash 断电或产品业务数据已通过现场验收。


## 清扫车 PSK 接入（2026-10-09，内部）

清扫车复用送餐车 1.1.48 / 平台 d3e3f7a 的专用 CA HTTPS 首次引导，
固定 `https://zxwl.ntiov.com:20444`、`UFS:enrollment_ca_v1.pem`，与 OTA CA 分离。
产品 Init 调用 RequirePsk；清扫车 MQTT profile 为 1，送餐车仍为 0。

首次使用 StartFirst；旧设备显式管理员 start_psk_enrollment 的 202 QoS1 确认后开始 HTTPS，
审批取得完整 tls_credentials 后使用有历史 active 配置的 Start，并走同一 trial/ConfigStore。
等待响应 15 秒、审批窗口 10 分钟；过期等 HTTP 清理完成再恢复历史 MQTT。
set_mqtt_psk 仅用于已激活 PSK 连接的更高代次轮换。服务端只扩展 sweeper_vcu，
不改变 legacy Enrollment 默认或巡防策略；不增加第二套 listener/inventory。

本次未修改送餐车适配。清扫车针对 RDY 丢失 HTTP 命令，在物理 reset 后将未完成的
核心 HTTP 阶段送回原重试流程，保留 request/token。历史生产验收材料保持原样。
PSK MQTT 无证书时间依赖不代表首次 HTTPS 无时间依赖；固定模组的证书有效期/主机名
限制仍存在，未关闭 seclevel 或证书时间/签名/链检查。
