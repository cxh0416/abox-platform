# A-BOX 内部授时协议 V2.0

内部资料，不作为业务对接协议对外交付。

授时沿用内部 MQTT V2 包络，与公共业务 `sync_state` 独立。设备与平台必须使用服务器身份认证成功的 TLS MQTT 连接，两个 Topic 均为 QoS 1、retain=false。

| 方向 | Topic |
|---|---|
| 设备发布、平台订阅 | `/zxwl/abox/{vid}/internal/time/request` |
| 平台发布、设备订阅 | `/zxwl/abox/{vid}/internal/time/response` |

设备 ACL 只允许发布自身 request、订阅自身 response。平台只响应当前有效设备身份。收到 retained 请求必须丢弃；响应不得作为业务在线状态、心跳或 `sync_state` 响应使用。

请求：

```json
{"version":"2.0","timestamp":0,"data":{"requestId":"ts-abcdef-1","command":"sync_time","params":{}}}
```

`timestamp` 为设备消息生成时的 UTC Epoch 毫秒整数，尚未获得可信 UTC 或可信时间失效时为 0；不用于校时计算或时钟一致性门禁。`requestId` 格式为 `ts-{1至16位小写十六进制关联前缀}-{1至8位小写十六进制序号}`，用于当前待处理请求的响应关联；同次运行内序号持续递增，重启后允许重复，不是认证凭据或跨重启新鲜度证明。平台收到有效请求时重新取时，不按历史同名 ID 复用授时结果。请求最长 512 字节，`data` 仅包含示例中的三个字段，不得含重复 JSON 键。

成功响应：

```json
{"version":"2.0","timestamp":1800000000000,"data":{"requestId":"ts-abcdef-1","code":200,"result":{"serverUtcMs":1800000000000,"errorMs":12}}}
```

`serverUtcMs` 为平台提交响应发布之前采样的 UTC；`errorMs` 为该参考时间的估计最大误差，范围 0～250 毫秒。设备通过本地 monotonic 记录发送、接收时刻，响应仅对当前尚未完成的相同 `requestId` 有效。Epoch 字段必须传输为安全整数范围内的十进制整数，禁止浮点数、科学计数法和字符串。

平台参考时间不可用：

```json
{"version":"2.0","timestamp":1800000000000,"data":{"requestId":"ts-abcdef-1","code":4003,"msg":"trusted time unavailable"}}
```

失败响应不得更新设备 UTC 基准。设备不得以一次连接成功、CCLK 初始化成功或无有效响应的查询尝试代替授时成功。

设备采用清洁 MQTT 会话，不接续旧会话的待投递消息。此机制与请求超时、当前连接检查不能保证识别跨重启且恰好同 ID 的延迟旧响应。授时失败应后台重试，不作为心跳、查询或网络恢复的总门禁；依赖有效期的请求仍按公共协议检查可信时间。
