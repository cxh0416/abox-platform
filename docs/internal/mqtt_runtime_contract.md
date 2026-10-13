# ABox MQTT runtime 统一配置合同

本文是 ABox Platform 内部实现与运维合同，不属于公共业务 MQTT V4 协议。

## 1. 统一 runtime 配置

所有产品进入公共 MQTT runtime 前，必须映射为 `ABoxMqttConfig`：

| 字段 | 语义 |
|---|---|
| `host` | Broker 主机名或地址 |
| `port` | Broker 端口 |
| `username` | MQTT 用户名 |
| `password` | MQTT 密码 |
| `tls_enabled` | `0` 使用明文；`1` 使用 TLS |
| `tls_profile_id` | TLS profile 标识，由产品/平台集成层解析为 CA 与 TLS 参数 |

`tls_enabled=0` 时不得申请或绑定 SSL context，也不得执行 CA、TLS profile 或
MQTT SSL binding 配置。`tls_enabled=1` 时必须使用指定 profile 和
`tls_credentials.mode` 完成认证、SSL context 配置和 MQTT SSL binding；
CA 模式执行 CA/证书及时间校验，PSK 模式执行既有 PSK 身份和独立秘密校验
（见“TLS 认证模式”）。任一步失败都按 candidate 失败处理。

正式开关只能来自 runtime 配置。构建宏可以裁剪测试目标，但不得作为产品切换
明文/TLS 的正式机制；交付固件必须同时保留两种连接能力。

产品 Flash ABI 不属于 Platform 合同。送餐车可继续使用
`BOARD_CONFIG_FLAG_MQTT_TLS`，底盘 V5 可继续使用 `mqtt_tls_enabled`，新产品也可
使用自己的结构；产品适配层只负责加载、校验、持久化和映射，不得让 Platform
包含、强转或依赖产品结构布局。

## 2. 唯一 candidate trial

Broker、端口、凭据和 TLS 策略共同组成一个不可拆分的 candidate。所有组合均走
同一个 `abox_mqtt_trial`：

```text
active -> candidate -> prepare -> connect -> subscribe -> proof
       -> persist + readback -> commit
       \-- 任一失败/取消/超时 -> restore active
```

TLS 初始化显式清除 `ignorecertitem`、`ignoreinvalidcertsign` 和
`ignoremulticertchainverify`，同时启用证书时间校验；任何设置失败都阻止连接。
不得依赖模组默认值或上一连接留下的校验策略。SNI 不能替代实机主机名负向验证。

该流程同等覆盖 `plain -> TLS`、`TLS -> plain`、`TLS -> TLS` 和
`plain -> plain`。`PREPARED` 只表示 candidate 所需的 TLS 绑定或明文解绑已完成；
`CONNECTED` 不能替代 `SUBSCRIBED`，通用 MQTT ACK 不能替代与本轮 session 和
proof ID 绑定的 `PROVED`。只有 `SAVED`（持久化并回读成功）后 active 才改变。

恢复必须停止 candidate、撤销其 SSL 绑定/租约，并按原 `active` 的完整配置恢复
连接和订阅。若 active 为 TLS，恢复也不得降级明文。

## 3. 禁止隐式降级

active 的 `tls_enabled=1` 时，证书、时钟、SSL context、binding、连接、订阅或
proof 失败均不得尝试同一 host/port 的明文连接。只有收到明确授权且
`tls_enabled=0` 的新 candidate，并完整通过明文 trial 后，才允许持久化和切回
明文。

`ABoxMqttTls_Start(..., enabled, ...)` 是该合同的既有执行入口：适配层把
`tls_enabled` 传入此入口，不得另建第二套 TLS 开关或旁路状态机。

## 4. 统一内部运维字段

所有 ABox 产品的 `set_mqtt` candidate 必须同时提供：

- `host`、`port`、`username`、`password`；
- `tlsEnabled`（Boolean）；
- `tlsProfileId`（0～255 的 Integer）。

产品若还允许变更 VID，可保留产品字段，但 VID 不属于 `ABoxMqttConfig`。
`get_info` 必须返回 `mqttHost`、`mqttPort`、`mqttTls` 和
`mqttTlsProfileId`。这些字段报告当前已提交 active，而不是正在试连的 candidate。

## 5. 产品接入边界

底盘工作包 7 按本合同把 V5 配置映射到 `ABoxMqttConfig`，接入已有公共 TLS runtime
和 `abox_mqtt_trial`，并补四种切换、失败恢复、掉电保存回读及禁止降级测试。
送餐车后续把现有 flag 映射到同一语义；不得要求两产品 Flash struct 对齐。

主机测试只能证明合同状态机和适配逻辑。真实 CA、模组 TLS 参数、Broker、订阅、
proof、明文/TLS 双向切换和复位恢复仍需分别保留实机证据。

## TLS 认证模式

连接配置的 `tls_credentials.mode` 选择 CA(0) 或 PSK(1)，与产品 profile ID 独立。
PSK 使用固定 TLS 1.2 / 0xCCAC 及 32 字符随机 ASCII；复用同一 runtime、租约、
AT owner 和 trial，不依赖 OTA CA/CCLK 才能建链。普通日志不接收秘密回显。
配置、工位注入、轮换与服务端边界见 [内部 PSK 运维](mqtt_psk_operations.md)。
明确失败的 QMTOPEN:-1 表示没有建立客户端；停止流程等待延迟结果再决定关闭，
保留 URC drain 与未知结果隔离，避免失败候选因关闭不存在客户端而阻塞回滚。
