# V4 公共入网与运维合同

适用于声明公共 MQTT V4 的 A-Box 产品。产品登记固定身份、业务 Profile、硬件合同与 TLS profile；平台根据注册身份执行公共入网、在线、授时和运维，不按客户名称派生产品标识。产品业务动作与硬件安全由产品实现。

## 入网、在线和授时

首次入网使用已认证 HTTPS，设备声明固定硬件合同；平台按注册信息核对身份，发放每设备 MQTT 凭据和 TLS 目标。TLS 的 CA 或 PSK、对应证书或密钥注入、模组支持、时间和服务端监听器必须与设备实际能力一致。设备不得将明文候选作为成功入网或连接降级路径。仅在完成连接、订阅、独立心跳证明、持久化与读回之后确认首次入网。入网进行中的唯一 AT owner 和公共恢复由 `ABoxAppService`、`ABoxEnrollmentService` 与既有 MQTT runtime 管理。

在线状态以匹配已登记产品的 Manifest 和新的非保留心跳为准；日志、响应、Broker PUBACK 和保留报文不能代替在线心跳。网络变化后沿用公共重连与 Manifest/Heartbeat/State 顺序。授时使用 [内部授时 V2](../protocols/A-BOX内部授时协议-V2.0.md)：TLS 认证成功后，设备以当前连接序号匹配请求和响应；服务器按注册产品及激活设备身份归属处理，授时服务和设备时钟质量分别核验。TLS 连接成功不等于授时成功。

## MQTT 受控配置

平台通过内部 V2 `internal_config` 的 `set_mqtt` 提交 `vid`、`host`、`port`、`username`、`password`、`tlsEnabled=true` 和整数 `tlsProfileId`。身份不变；TLS CA/PSK 模式和设备已有独立 PSK 凭据保持不变，PSK 轮换使用独立事务。只接受目标 TLS，活动连接也必须为 TLS。产品解析应复用 `ABoxCloudInternalV2_MqttTlsCandidate`，并使用已有 trial、executor 与 ConfigStore；设备在业务忙、OTA 或其它冲突状态时拒绝。

`get_info` 至少返回 `vid`、`fwVersion`、`otaBusy`、`mqttHost`、`mqttPort`、`mqttTls`、`mqttTlsActive`、`mqttTlsProfileId`、`mqttAuthMode`（`ca` 或 `psk`）、`trustedTime`，及 `maintenanceProtocols.mqttConfigTransaction=1`。`mqttTls` 描述已保存配置；`mqttTlsActive` 描述当前已认证的活动连接，`trustedTime` 表示可信授时，三者不可相互代替。不能返回 MQTT 密码或 PSK 密钥。

设备收到配置后返回 `202 MQTT_CANDIDATE_TESTING`；接受响应确认后才进行候选试连。设备只有在完成订阅、关联心跳证明和配置持久化回读后返回 `200 MQTT_CONFIG_SAVED`；失败恢复旧 TLS 配置后返回 `500 MQTT_CANDIDATE_RESTORED`。平台使用关联 requestId、新心跳、独立配置和认证模式回读确认结果；响应丢失时仅只读核对，不能重发写入。切换成功不代表授时成功。

## 注册与验证

产品在服务端产品和硬件合同中登记，硬件合同使用 TLS 策略及真实 profile；产品通过统一生命周期入口、公共组件和业务回调接入。产品固件需实际提供上述字段和结果，不能仅靠注册表宣称支持。共同验证覆盖入网、TLS 负向验证、心跳归属、重连、可信授时、配置成功及失败恢复、重启恢复与失联结果不明；产品额外验证业务安全与资源预算。主机测试不能代替蜂窝网络、模组、Broker、Flash 和现场运行的验证。
