# A-BOX 设备公共 MQTT 协议

**文档修订：V4.0 正式冻结版**

**报文版本：`4.0`**

**状态：Frozen Public Contract（正式冻结）**

**修订日期：2026-09-30**

五 Topic、公共消息模型、路由、Manifest 能力治理及本文参数均已冻结。兼容扩展遵循第 9 节；改变既有公共语义必须升级公共协议主版本。

| 查阅项 | 定义位置 |
|---|---|
| Topic、QoS、会话、身份 | 第 2 节 |
| 公共字段类型、大小、词法 | 第 3 节 |
| 请求、响应、错误码、有效期、重试 | 第 4 节 |
| Report、去重、实例、顺序 | 第 5 节 |
| 完整状态、sync_state | 第 6 节 |
| 心跳、Manifest、版本和在线确认 | 第 7 节 |
| 命名与 Profile 登记 | 第 8 节 |
| 兼容性 | 第 9 节 |
| Canonicalization、摘要、固定向量 | 规范性附录 A |

## 1. 范围与路由

本协议定义 A-BOX 与业务平台之间的公共通信契约。业务 Profile 定义命令参数、操作类别、`200` 成功边界、结果、报告、实例、状态枚举及业务安全语义；相同能力必须复用相同语义，不得因车型、客户或项目重新定义公共框架。

| 对象 | 权威规则 |
|---|---|
| 设备身份 | 每台物理 A-BOX 由平台分配全局唯一 `deviceId`，用于 Topic，不要求在每条 Payload 中重复 |
| Request 路由 | `command + params`；执行前必须唯一确定目标操作 |
| Report 路由 | 非心跳主动业务消息统一进入 `/report`，按 `reportType + reportName` 路由；业务数据放在 `payload` |
| Profile | 能力、版本及业务语义的定义单位；一台设备可实现多个，不要求每条 Request/Report 携带 Profile 标识；Manifest 不参与逐条消息路由 |
| 单实例 | 不要求实例标识 |
| 多实例 | Report 使用 `instanceId`，请求目标字段由 Profile 在 `params` 中定义；具体约束见第 5.3 节 |

内部总线、拓扑、轮询、控制仲裁、故障恢复算法不属于公共协议。设备信息、日志、配置、诊断、OTA 等继续使用独立 `/internal/*` 协议，允许离线恢复的运维业务不得与公共实时控制混用。

## 2. Topic、传输与会话

### 2.1 Topic、QoS、Retain

| 方向 | 消息 | Topic | QoS | Retain |
|---|---|---|---:|---|
| 平台 → A-BOX | Request | `/zxwl/abox/{deviceId}/request` | 1 | false |
| A-BOX → 平台 | Response | `/zxwl/abox/{deviceId}/response` | 1 | false |
| A-BOX → 平台 | state / event Report | `/zxwl/abox/{deviceId}/report` | 1 | false |
| A-BOX → 平台 | telemetry Report | `/zxwl/abox/{deviceId}/report` | 0 | false |
| A-BOX → 平台 | Heartbeat | `/zxwl/abox/{deviceId}/heartbeat` | 0 | false |
| A-BOX → 平台 | Manifest | `/zxwl/abox/{deviceId}/manifest` | 1 | true |

Profile 不得修改 Topic 结构或改变 Report QoS。平台可将上表 A-BOX → 平台的 Topic 中 `{deviceId}` 替换为 `+` 统一订阅；`/zxwl/abox/+/report` 订阅必须请求 QoS 1，避免 state/event 实际交付被降为 QoS 0。

不得通过 Retained Request 表达控制意图。telemetry 固定 QoS 0；必须可靠投递的数据应按实际语义归为 state/event，不得通过提升 telemetry QoS 规避分类。Report 不使用 Retain，状态恢复遵循第 6 节。

### 2.2 会话与自动重连

公共控制不得依赖 Broker 离线队列补发。

| MQTT 版本 | A-BOX 公共业务客户端必需设置 |
|---|---|
| 3.1.1 | `Clean Session = true` |
| 5.0 | 同时设置 `Clean Start = true`、`Session Expiry Interval = 0` |

上述会话要求只针对 A-BOX 公共业务客户端；平台消费者、Bridge 等服务端组件的持久会话由部署架构决定。

| 自动重连阶段 | 固定规则 |
|---|---|
| 首次连接 | 随机延迟 0～4 秒 |
| 连续失败 | 退避上限依次为 4、8、16、32、60 秒，后续保持 60 秒 |
| 实际延迟 | 每次从当前上限 50%～100% 取带设备差异的随机抖动，避免设备同步重连 |
| 重置 | 稳定在线 5 分钟后重置退避级别 |

A-BOX 必须自动重连。成功重连后的逻辑发送顺序为 **Manifest → 启动/恢复 Heartbeat → 补发所有完整 State → 正常业务**。该顺序约束设备发送逻辑；平台不得假定不同 Topic 经中间链路仍按此顺序到达。能力确认前到达的 Report 可以保存或缓存，但不能据此确认新能力可用。

### 2.3 身份、ClientId 与 ACL

| 项目 | 约束 |
|---|---|
| `deviceId` | 1～32 个 ASCII 字符，完整匹配 `^[A-Za-z0-9_-]+$`；禁止 `/`、`+`、`#`、空白和控制字符 |
| MQTT `ClientId` | 必须逐字等于 `deviceId` |
| 凭据 | 每设备独立；禁止多台生产设备共享同一可写公共业务凭据 |
| 服务端绑定 | 已认证设备身份唯一绑定一个 `deviceId`；Broker 绑定凭据、ClientId、Topic ACL，不得仅信任客户端声明的身份或 ClientId |
| 发布权限 | 只能发布自身 `response`、`report`、`heartbeat`、`manifest` |
| 订阅权限 | 只能订阅自身 `request` |

ACL 必须由服务端身份绑定生成；禁止跨设备发布/订阅、用通配符扩大设备授权或构造 Topic 绕过绑定。

生产允许明文 MQTT 或 MQTT over TLS，两者身份、独立凭据与 ACL 要求相同。TLS 最低 1.2，支持时优先 1.3；设备必须校验证书链、主机名、有效时间并发送正确 SNI。明文不提供保密性和传输层防篡改，部署方必须明确接受风险，不得标为 TLS。凭据轮换通过受控配置或生产 provisioning，不能走公共业务 Topic；切换后必须撤销旧凭据。

## 3. 公共 JSON 与字段限制

### 3.1 外层 Schema

所有 Payload 使用 UTF-8 JSON，禁止 BOM；固定 Topic 段使用小写。

| 字段 | 类型 | 必填 | 定义 |
|---|---|---|---|
| `version` | String | 是 | 固定 `"4.0"` |
| `timestamp` | Integer | 是 | 消息生成时间，UTC Unix Epoch 毫秒 |
| `data` | Object | 是 | 对应消息内容 |

```json
{
  "version": "4.0",
  "timestamp": 1770186088333,
  "data": {}
}
```

### 3.2 数值、扩展及非法报文

| 项目 | 规则 |
|---|---|
| 所有公共数字 | 仅允许 `-9007199254740991..9007199254740991` 的安全整数；小数、指数形式、负零、NaN、无穷值均非法 |
| Epoch 毫秒 | `timestamp`、`expiresAt` 及未来 Epoch 字段具有 64 位整数语义；必须输出非负十进制整数，不接受字符串、小数、指数、前导 `+` 或负零；不得因浮点转换截断、舍入或输出科学计数法 |
| 重复 JSON Key | 任意 Object 重复 Key 均非法，必须拒绝，不能选择其中一个值 |
| 未知字段 | 必须忽略可安全忽略的扩展字段，不能仅因未知可选字段丢弃合法消息 |
| 未知业务枚举 | 必须保留原值，可降级展示 `unknown`；不得解释为成功、完成、停止、关闭、正常或其他安全状态 |

整数边界和非法词法向量见[规范性附录 A](#a1-v4-json-canonicalization)。

### 3.3 长度与大小

| 项目 | 上限或格式 |
|---|---|
| 单条 MQTT Payload | 完整 UTF-8 JSON 最大 1024 字节，不含 Topic 和 MQTT 包头 |
| `params` / `result` / `payload` | 各自序列化后最大 768 字节，仍受整包上限限制 |
| `requestId` | 1～64 个 ASCII 字符，`^[A-Za-z0-9][A-Za-z0-9._:-]*$` |
| `reportId` | 1～96 个 ASCII 字符，同上字符表达式 |
| `streamId` | 1～64 个 ASCII 字符，同上字符表达式 |
| `instanceId` | 1～32 个 ASCII 字符，`^[A-Za-z0-9_-]+$` |
| `command` / `reportName` / Profile 名称 | 最长 64 个字符，小写 snake_case；命名规则见第 8 节 |
| `firmware` | 最长 64 个 ASCII 字符 |
| `msg` | 最长 128 个 UTF-8 字节，禁止控制字符 |
| Manifest | 最多 16 个 Profile |

超限数据不得在公共 Topic 中自行分片；大文件、日志、固件使用 `/internal/*` 对应通道。

## 4. Request、Response 与执行规则

### 4.1 Request.data Schema

| 字段 | 类型 | 必填 | 定义 |
|---|---|---|---|
| `requestId` | String | 是 | 平台生成的全局唯一请求标识 |
| `command` | String | 是 | 业务命令 |
| `expiresAt` | Integer | 按操作类别 | UTC Epoch 毫秒失效时间 |
| `params` | Object / null | 是 | 业务参数，无参数时显式 `null` |

```json
{
  "version": "4.0",
  "timestamp": 1770186088333,
  "data": {
    "requestId": "3fa85f64-5717-4562-b3fc-2c96063f8666",
    "command": "wing_control",
    "expiresAt": 1770186093333,
    "params": {
      "action": "ascend"
    }
  }
}
```

### 4.2 操作类别与重试

每个可执行操作必须在执行前唯一确定类别。同一 command 的不同参数组合可分属不同类别，Profile 须按“命令 + 判定条件”分别登记；匹配规则见第 8 节。

| 类别 | 接受与安全规则 | 相同 requestId、相同内容的重试 |
|---|---|---|
| `QUERY` | 不应产生不可逆物理副作用；`expiresAt` 默认可选；`sync_state` 属于 QUERY | 允许重新执行及重新生成同步结果；新 Report 使用新 reportId，关联原 requestId |
| `DISCRETE_ACTION` | expiresAt 必填；Profile 定义最大有效期；必须幂等，MQTT 重投不得重复动作，必须具备跨重启保护 | 不得重复动作；应返回首次完整响应；持久化异步最终结果的策略由 Profile 定义 |
| `CONTINUOUS_SESSION` | 不得依靠持久化每条高频请求保障安全；Profile 至少定义等价的 sessionId、sequence、lease/timeout 机制；会话失效、断网、租约超时、序号异常进入规定安全状态；requestId 幂等不能替代这些机制 | 按 Profile 会话、序号、租约规则 |
| `SAFETY_STOP` | 由 Profile 定义；应天然幂等，可有特殊过期规则，可重复执行或绕过普通 Busy；不得绕过认证、基本合法性及必要参数校验 | 按 Profile 安全规则，可允许重复执行 |

离散动作 Profile 必须声明 `persistent_idempotency`、`session_constraint` 或等价安全方案。重启会丢失幂等记录时，必须至少采用持久保存 `requestId + 摘要 + 首次响应`、Profile 级启动会话约束，或经评审证明等价安全的机制。QUERY 重启后允许重做；安全停止按 Profile 规则。

### 4.3 有效期与校验顺序

1. `expiresAt` 是最晚允许开始接受执行的时刻；接受后的持续时间由 Profile 超时、租约或安全规则决定。
2. 每个要求 expiresAt 的操作必须定义 `maxValidity`；离散动作不得超过 30 秒，Profile 只能更短。
3. 原始有效期须满足 `0 < expiresAt - request.timestamp <= maxValidity`。先比较大小再相减，避免无符号下溢。不满足返回 `4001`，既有 Profile 明定的响应码例外仍有效。
4. 新请求在可信当前 UTC 满足 `currentTime >= expiresAt` 时返回 `4005` 并拒绝执行；不得以 `expiresAt - currentTime` 判断原始最大有效期。
5. `timestamp` 用于生成时间、审计、排序及原始有效期，不是严格时钟一致性门禁；重试应保留原始 timestamp 和 expiresAt。
6. 依赖 expiresAt 的新请求必须使用可信时间；未同步、过久未校时或估计误差不能确认尚未到期时返回 `4003`，不得静默忽略有效期。
7. 合法重复请求先检查身份冲突并返回已保存结果；已接受请求的重复投递不得因当前过期或授时不可用而重做或改变已保存结果。

### 4.4 请求一致性与保护窗口

逻辑内容由 `command + 规范化 params + 存在时的 expiresAt` 决定，外层 timestamp 不参与。不得按原始 JSON 字符串、Object 顺序或无意义空白判断相等。摘要对象、规范化及固定 SHA-256 算法见附录 A。

相同 requestId 的逻辑内容任一不同，返回 `4006`，拒绝新执行，不得覆盖原结果。有 expiresAt 的离散动作幂等记录至少应保留至 **expiresAt + 10 分钟**（固定保护窗口）。

### 4.5 Response.data Schema 与成功边界

| 字段 | 类型 | 必填 | 定义 |
|---|---|---|---|
| `requestId` | String | 是 | 原请求标识 |
| `code` | Integer | 是 | 请求处理结果 |
| `msg` | String | 是 | 展示与诊断文本 |
| `result` | Object | 否 | 同步结构化结果；无结果时省略，不发送 `null` |

```json
{
  "version": "4.0",
  "timestamp": 1770186088456,
  "data": {
    "requestId": "3fa85f64-5717-4562-b3fc-2c96063f8666",
    "code": 200,
    "msg": "operation accepted"
  }
}
```

`200` 表示达到 Profile 对当前操作定义的成功边界，不统一表示机械完成。Profile 必须为每个操作精确登记：例如 wing ascend 可表示接受并进入执行流程，而 open_locker 表示所有目标反馈打开。

### 4.6 公共响应码与异常响应

| code | 名称 | 含义 |
|---:|---|---|
| `200` | `SUCCESS` | 达到 Profile 成功边界 |
| `400` | `BUSINESS_FAILED` | Profile 明定的业务执行失败 |
| `4001` | `INVALID_REQUEST` | JSON 结构、必填字段、参数、枚举等非法 |
| `4002` | `UNSUPPORTED_COMMAND` | 不支持 command 或可执行操作 |
| `4003` | `BUSY` | 业务繁忙、互斥操作或时间敏感请求的可信时间不可用 |
| `4004` | `RESERVED` | 公共保留 |
| `4005` | `REQUEST_EXPIRED` | 请求已过期 |
| `4006` | `REQUEST_ID_CONFLICT` | 相同 requestId 对应不同逻辑内容 |
| `500` | `MESSAGE_TOO_LARGE` | 无法在单包上限内生成完整响应 |
| `5001` | `INTERNAL_ERROR` | A-BOX 内部异常 |

Profile 专用码只能在该 Profile 内解释。Payload 无法解析、存在重复 Key，或缺少合法、唯一、可信 requestId 时，设备可直接丢弃并记录诊断，不要求生成无法可靠关联的 Response。

## 5. Report、去重、实例与顺序

### 5.1 Report.data Schema

| 字段 | 类型 | 必填 | 定义 |
|---|---|---|---|
| `reportId` | String | 是 | 本次逻辑报告唯一标识 |
| `reportType` | String | 是 | state / event / telemetry |
| `reportName` | String | 是 | 业务上报名称 |
| `instanceId` | String | 多实例时 | 业务实例 |
| `streamId` | String | 严格有序流时 | 流标识，与 sequence 成对 |
| `sequence` | Integer | 严格有序流时 | 流内序号，与 streamId 成对 |
| `requestId` | String | 关联请求时 | 请求的异步后续 Report 必须携带原标识 |
| `payload` | Object / null | 是 | 业务数据；完成、失败、停止语义由 Profile 定义；公共层不定义统一 executionState |

| reportType | 语义 |
|---|---|
| `state` | 当前业务状态或完整状态快照 |
| `event` | 一次离散业务事件 |
| `telemetry` | 连续、高频、允许少量丢失的数据 |

Profile 不得增加 reportType；新业务语义使用新 reportName。QoS/Retain 只在第 2.1 节定义。

```json
{
  "version": "4.0",
  "timestamp": 1770186089000,
  "data": {
    "reportId": "69c06723-cb44-43db-905b-f912e97fdb50",
    "reportType": "state",
    "reportName": "wing_status",
    "payload": {
      "motionState": "ascending",
      "positionMm": 126
    }
  }
}
```

```json
{
  "version": "4.0",
  "timestamp": 1770186095000,
  "data": {
    "reportId": "b1177af0-26c7-4ce0-8899-21ba7181154d",
    "reportType": "event",
    "reportName": "wing_control_result",
    "requestId": "3fa85f64-5717-4562-b3fc-2c96063f8666",
    "payload": {
      "action": "ascend",
      "result": "completed"
    }
  }
}
```

### 5.2 reportId 与去重

新逻辑 Report 必须使用新 reportId；同一报告 QoS 重投保持原 ID。同 deviceId 在平台固定 **24 小时**去重保留期内不得复用 ID；平台按 `deviceId + reportId` 去重。默认生成 RFC 9562 UUIDv4；资源受限设备可使用其他方式，但仍须满足该唯一性要求。

### 5.3 instanceId 与状态主键

同设备存在同类多实例时，相关 Report 必须带 instanceId。Profile 须定义多实例支持、必填性、字符、最大长度、唯一性范围及请求目标字段，且服从第 3.3 节公共上限。

平台最新 state 主键固定为 `deviceId + reportName + instanceId`；单实例使用固定逻辑空值。Manifest 声明 instances 时，Report 实例必须属于当前已确认集合；未声明实例的消息应保存原始报文并标记能力配置不一致，不能自动加入 Manifest。

### 5.4 严格有序流

普通 Report 不要求序号；需严格顺序的 Profile 使用 streamId + sequence：

1. 两字段同时出现或同时不存在；sequence 为 uint32，从 1 递增。
2. uint32 回绕前更换 streamId；重启或流重新初始化必须生成新 streamId。
3. 仅在相同 `deviceId + reportName + instanceId + streamId` 内比较 sequence；不同 streamId 序号不能直接比较新旧。

## 6. 完整状态与 sync_state

### 6.1 完整状态及自动补发

每个有状态 Profile 必须登记完整状态 reportName；完整状态须无需历史消息即可重建当前可观察状态。设备启动完成且业务获得可靠状态后，以及 MQTT 重连后，必须补发全部当前启用 Profile 的完整状态；自动补发省略 requestId。平台持久保存最新状态并可发起 sync_state。

### 6.2 Core sync_state

| 项目 | 定义 |
|---|---|
| command / category | `sync_state` / `QUERY`，Core 保留命令 |
| params | `null` |
| `200` | 已接受并安排同步，不代表 Report 全部发布、Broker 或后续平台链路收到并处理 |
| result.expectedStates | 本轮权威预期状态集合 |

```json
{
  "version": "4.0",
  "timestamp": 1770186088333,
  "data": {
    "requestId": "a820dbf4-d62e-4f51-a389-f881616fa933",
    "command": "sync_state",
    "params": null
  }
}
```

```json
{
  "version": "4.0",
  "timestamp": 1770186088400,
  "data": {
    "requestId": "a820dbf4-d62e-4f51-a389-f881616fa933",
    "code": 200,
    "msg": "state synchronization scheduled",
    "result": {
      "expectedStates": [
        {
          "reportName": "wing_status"
        },
        {
          "reportName": "refrigeration_status",
          "instanceId": "1"
        },
        {
          "reportName": "refrigeration_status",
          "instanceId": "2"
        }
      ]
    }
  }
}
```

### 6.3 expectedStates 与完成判定

1. 条目必须有 reportName；单实例不得带 instanceId，多实例必须带；`(reportName, instanceId)` 组合不得重复。
2. 首次处理某 requestId 时确定集合；相同逻辑请求重试不得改变它。平台不得按 Manifest Profile 数量猜测待收状态数。
3. 本轮全部完整状态 Report 必须带原 requestId；平台按 requestId + expectedStates 判断收齐。
4. 同 requestId、同状态键可因重试有多个完整快照，每条新快照使用新 reportId；平台应以**最后接收到的完整状态快照**作为本轮该键结果。

### 6.4 重试、乱序与超时

| 情况 | 必需处理 |
|---|---|
| 同 requestId、同内容重试 | 沿用首次 expectedStates，重新发送集合中完整状态；新 Report 用新 reportId 并关联原 requestId；不得因当前 Manifest/能力变化改变集合 |
| 同步中 Manifest 变化 | 平台必须废弃未完成旧轮次，等待新 Manifest 在线确认后以新 requestId 同步；设备建立新集合 |
| Report 先于 Response 到达 | 平台应按 requestId 临时缓存完整状态，收到响应后按 expectedStates 匹配；不得仅因响应晚到丢弃合法结果 |
| 等待超时 | 从收到成功响应起固定 15 秒；仅代表未收齐，不能推断设备离线、故障、停止、关闭、正常或其他确定状态；可保留旧持久状态，标为不完整或未知新鲜度 |

## 7. Heartbeat、Manifest 与能力确认

### 7.1 Heartbeat.data Schema

| 字段 | 类型 | 必填 | 定义 |
|---|---|---|---|
| `firmware` | String | 是 | 当前固件版本字符串 |
| `manifestId` | String | 条件必填 | 第 7.3 节要求绑定能力时回显 |

```json
{
  "version": "4.0",
  "timestamp": 1770186088333,
  "data": {
    "firmware": "abox-4.0.0"
  }
}
```

使用 manifestId 的心跳在 data 中增加 `"manifestId":"sha256:..."`。完整能力清单不得放入心跳；心跳只证明 A-BOX 与 Broker 通信存活，不证明业务设备、总线或整车安全可用，业务状态通过 Report 表达。

心跳周期固定 **5 秒**，连续 **15 秒**未收到即离线，不允许 Profile/项目改写或主动增加协议级随机抖动。网络、调度少量抖动不改变离线边界。在线判定依据实际收到心跳的时间，不能用设备 timestamp；平台处理以 Broker 接收时间为准。

### 7.2 Manifest.data Schema

Manifest 用于核对实际能力、Profile 版本、资产台账及升级/配置后的能力变化。

| 字段 | 类型 | 必填 | 定义 |
|---|---|---|---|
| `manifestId` | String | 条件必填 | 规范化内容摘要，见第 7.3 节及附录 A |
| `firmware` | String | 是 | 当前固件版本 |
| `profiles` | Array | 是 | 当前实际启用能力 |
| `profiles[].name` | String | 是 | Profile 名称，集合内必须唯一 |
| `profiles[].version` | String | 是 | MAJOR.MINOR |
| `profiles[].instances` | Array&lt;String&gt; | 多实例时 | 完整实例集合，标识唯一；单实例必须省略 |

```json
{
  "version": "4.0",
  "timestamp": 1770186088333,
  "data": {
    "manifestId": "sha256:...",
    "firmware": "abox-4.0.0",
    "profiles": [
      {
        "name": "refrigeration",
        "version": "1.2",
        "instances": [
          "1",
          "2"
        ]
      },
      {
        "name": "wing",
        "version": "1.0"
      }
    ]
  }
}
```

instances 只声明能力配置，不是逐消息路由。Report 实例来自当前集合；增加、删除、改名必须重新发布 Manifest 并产生新 manifestId。

### 7.3 摘要要求、发布与在线确认

若同 firmware 因配置、装配、参数或项目差异可能对应不同 Profile 集合、版本或 instances，manifestId **必须存在并由心跳回显**。仅在同 firmware 唯一决定全部集合及版本时才可省略。摘要规则见附录 A。

设备必须在连接建立、固件版本变化、Profile 集合/版本变化、影响能力的配置变化后发布 Manifest。Retained Manifest 只代表最近声明，不证明当前在线。

| 模式 | 当前在线实例能力确认条件 |
|---|---|
| 带 manifestId | 当前在线心跳与 Retained Manifest 的 manifestId 相同 |
| 省略 manifestId | 满足 firmware 唯一决定能力的前提，且当前在线心跳 firmware 与 Retained Manifest 完全一致 |

确认前不得标记能力为当前在线实例已确认可用。只有收到报文版本 4.0 的当前在线心跳并确认 Manifest 才允许下发动作；版本或能力不明确时不得下发。

平台只应向同时满足“心跳在线、Manifest 已确认、支持目标 Profile、平台支持该 MAJOR、具体操作在设备版本能力内”的设备发动作。该检查不能消除发布前掉线竞态；设备仍必须执行有效期、幂等、租约及 Profile 安全保护。

### 7.4 Profile 版本兼容

MAJOR、MINOR 均为 `0..65535` 的 uint16 十进制数，无前导零，格式 MAJOR.MINOR。

| 变化或组合 | 规则 |
|---|---|
| MAJOR 变化 | 不兼容变化：修改字段语义/必填字段、删除能力、修改成功边界或 Report 基本语义 |
| MINOR 变化 | 只允许兼容扩展：新增 command/reportName、可忽略可选字段、专用响应码 |
| 平台不支持 MAJOR | 不得发动作；可保存未知 Report 原始数据，不得确认可控 |
| 相同 MAJOR，设备 MINOR 较高 | 使用已知兼容子集，忽略未知兼容扩展 |
| 设备 MINOR 较低 | 不得使用更高 MINOR 引入的能力 |

## 8. Registry 与 Profile 登记要求

Command、ReportName、Profile 以机器可校验 Registry 为唯一登记入口。全平台必须维护稳定的三类 Registry；公共 Core 登记见 [v4/registry.json](v4/registry.json)。

| 对象 | 命名与唯一性 |
|---|---|
| command | 每个名称只登记一项全局能力语义；多个 Profile 可引用，不能同名异义 |
| reportName | 全局唯一，不能同名异义 |
| Core 命令 | sync_state 等保留名称不得被 Profile 覆盖或重释 |
| 命名 | 小写、下划线连接；不得用车型/客户/项目名作业务前缀；相同语义复用名称；仅产品独有能力使用产品前缀 |

command 表示能力而非输出器件、数量或内部对象。各 Profile 可定义自身目标、成功边界、结果及状态，但不得改变全局语义。例如 power_on/off 表示请求电源控制输出进入开启/关闭目标，不保证负载通断或授予运动权限。

同设备引用同名命令的多个 Profile 必须由 command + params 唯一确定目标，否则不得同时启用；不得有路由冲突。多个 operationMatcher 必须且只能命中一个；0 个或多个均返回 4001，禁止动作，不得靠匹配顺序消歧。整个 command 所有参数组合类别一致时可省略 operationMatcher。

| Registry | 最小登记字段 |
|---|---|
| Command | command、introducedVersion、semanticDescription、applicableProfiles |
| ReportName | reportName、ownerProfile、introducedVersion、semanticDescription |
| Profile | profileName、supportedMajorVersions、currentVersion |

| Profile 操作登记字段 | 定义 |
|---|---|
| command / operationMatcher | 名称及参数匹配条件 |
| category | 四种操作类别之一 |
| params | 参数 Schema |
| expiresAt / maxValidity | 必填/可选/特殊规则及最大有效期 |
| 200Boundary / result | 精确成功边界及同步结果 |
| rebootPolicy / errorCodes | 跨重启策略及专用码 |

| Profile Report 登记字段 | 定义 |
|---|---|
| reportName / reportType / payload | 名称、三种类型之一及 Schema |
| fullState | 是否完整状态 |
| instanceId | 是否必需；多实例须与 Manifest instances 一致 |
| orderedStream | 是否需要 streamId + sequence |
| requestCorrelation | 是否可能关联 requestId |

## 9. 平台处理与 V4 兼容性

平台必须先校验公共外层，再按公共路由处理；去重、最新状态主键、乱序、在线判定、未知字段/枚举分别遵循第 3、5～7 节。平台内部消息队列、存储或服务组织不得改变这些语义。

| 变化 | 版本要求 |
|---|---|
| 新 Profile、command、reportName；Profile MINOR 扩展；params/payload 可忽略可选字段；专用响应码；Manifest 兼容可选字段 | 可继续 `version="4.0"` |
| 公共字段名、类型、必填性或既有语义；五 Topic 基本资源模型；requestId 基本幂等；reportId 去重；四类操作或 reportType 基本语义；会话安全边界 | 必须升级公共协议主版本 |

全部既有参数及“五 Topic + 五类公共消息 + command 控制路由 + reportType/reportName 上报路由 + Manifest 能力治理”架构不得改变。除发现不可实现的安全缺陷外，后续评审不得重新引入强制 Component Resource Model 或重新拆分公共 Topic。

## 附录 A（规范性）：Canonicalization 与摘要

### A.1 V4 JSON Canonicalization

固定采用 **语义集合归一化 + RFC 8785 JCS**：

1. 解析时拒绝重复 Key、非法数字及不符合字段合同的值。
2. Manifest profiles 先按 name 的 Unicode 码点顺序稳定排序；instances 按实例标识 Unicode 码点顺序稳定排序，重复实例非法。
3. 其他数组保持顺序，只有 Core/Profile 明定为集合时可先排序。
4. 随后执行 RFC 8785 JCS：对象递归排序、UTF-8 无 BOM、无额外空白、JCS 字符串转义。
5. 不执行 Unicode Normalization；null 保留为 JSON null。字符串比较基于解码后的 Unicode 字符值，统一 UTF-8 Canonicalization。
6. 数字词法及安全整数范围遵循第 3.2 节。

对象字段乱序且值相同须产生相同 Canonical Bytes；普通 `[1,2]` 与 `[2,1]` 不同。Manifest Profile 顺序或 instances 顺序不同但集合相同须摘要一致；重复实例不能进入摘要。

### A.2 请求摘要

持久化或计算摘要时，必须按 A.1 规范化逻辑对象：

```json
{"command":"...","params":null,"expiresAt":1770186093333}
```

command 和 params 必须参与；携带 expiresAt 时必须参与。不使用 expiresAt 时须省略，不能补 null。缺 params 非法，params:null 与 params:{} 不等价。业务集合先按 Profile 声明归一化。

固定 `Request Digest = lowercase_hex(SHA-256(Canonical Bytes))`，64 位小写十六进制，不得选其他算法；外层 timestamp 不参与。

### A.3 Manifest 摘要

摘要输入为 Manifest data 除 manifestId 自身外的**全部字段**；V4 为 `{"firmware":"...","profiles":[...]}`。未来兼容新增字段默认参与，除非公共协议明确声明不参与。外层 version/timestamp 不参与。

固定 `manifestId = "sha256:" + lowercase_hex(SHA-256(canonical_manifest_data))`，格式 `sha256:<64位小写十六进制>`，不得选其他算法。

### A.4 固定测试向量

[v4/test-vectors.json](v4/test-vectors.json) 是规范性固定向量，跨设备和平台必须一致使用，覆盖 Manifest/请求逻辑对象 → Canonical Bytes → SHA-256，以及重复 Key、Unicode、对象乱序、普通数组换序、整数边界和非法数值词法。
