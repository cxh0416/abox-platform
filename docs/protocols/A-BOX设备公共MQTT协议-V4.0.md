# A-BOX 设备公共 MQTT 协议 V4.0

| 项目 | 内容 |
| --- | --- |
| 适用产品 | A-BOX 公共业务设备与业务平台 |
| 文档修订号 | V4.0（业务数值规则修订） |
| 报文版本 | `4.0` |
| 文档状态 | Public Contract（公共规范） |
| 修订日期 | 2026-10-15 |
| 依赖协议 | [RFC 8785 JCS](https://www.rfc-editor.org/rfc/rfc8785.html)；产品业务语义由对应 Profile 定义 |

**首次接入入口：** 先按[第 1.1 节](#11-按任务阅读)选择阅读路径，再依[第 7.5 节](#75-平台首次接入与恢复流程)完成接入。容易混淆的标识、版本及时间边界分别见[第 1.2 节](#12-关键概念对照)和[第 1.3 节](#13-时间边界对照)。产品命令参数、状态及动作成功边界同时查阅对应产品协议。

## 目录

- [1. 范围与路由](#1-范围与路由)
  - [1.1 按任务阅读](#11-按任务阅读)
  - [1.2 关键概念对照](#12-关键概念对照)
  - [1.3 时间边界对照](#13-时间边界对照)
- [2. Topic、传输与会话](#2-topic传输与会话)
  - [2.1 Topic、QoS、Retain](#21-topicqosretain)
  - [2.2 会话与自动重连](#22-会话与自动重连)
  - [2.3 身份、ClientId 与 ACL](#23-身份clientid-与-acl)
- [3. 公共 JSON 与字段限制](#3-公共-json-与字段限制)
  - [3.1 外层 Schema](#31-外层-schema)
  - [3.2 数值、扩展及非法报文](#32-数值扩展及非法报文)
  - [3.3 长度与大小](#33-长度与大小)
- [4. Request、Response 与执行规则](#4-requestresponse-与执行规则)
  - [4.1 Request.data Schema](#41-requestdata-schema)
  - [4.2 操作类别与重试](#42-操作类别与重试)
  - [4.3 有效期与校验顺序](#43-有效期与校验顺序)
  - [4.4 请求一致性与保护窗口](#44-请求一致性与保护窗口)
  - [4.5 Response.data Schema 与成功边界](#45-responsedata-schema-与成功边界)
  - [4.6 公共响应码与异常响应](#46-公共响应码与异常响应)
- [5. Report、去重、实例与顺序](#5-report去重实例与顺序)
  - [5.1 Report.data Schema](#51-reportdata-schema)
  - [5.2 reportId 与去重](#52-reportid-与去重)
  - [5.3 instanceId 与状态主键](#53-instanceid-与状态主键)
  - [5.4 严格有序流](#54-严格有序流)
- [6. 完整状态与 sync_state](#6-完整状态与-sync_state)
  - [6.1 完整状态及自动补发](#61-完整状态及自动补发)
  - [6.2 Core sync_state](#62-core-sync_state)
  - [6.3 expectedStates 与完成判定](#63-expectedstates-与完成判定)
  - [6.4 重试、乱序与超时](#64-重试乱序与超时)
- [7. Heartbeat、Manifest 与能力确认](#7-heartbeatmanifest-与能力确认)
  - [7.1 Heartbeat.data Schema](#71-heartbeatdata-schema)
  - [7.2 Manifest.data Schema](#72-manifestdata-schema)
  - [7.3 摘要要求、发布与在线确认](#73-摘要要求发布与在线确认)
  - [7.4 Profile 版本兼容](#74-profile-版本兼容)
  - [7.5 平台首次接入与恢复流程](#75-平台首次接入与恢复流程)
- [8. Registry 与 Profile 登记要求](#8-registry-与-profile-登记要求)
- [9. 平台处理与 V4 兼容性](#9-平台处理与-v4-兼容性)
- [附录 A（规范性）：Canonicalization 与摘要](#附录-a规范性canonicalization-与摘要)
  - [A.1 V4 JSON Canonicalization](#a1-v4-json-canonicalization)
  - [A.2 请求摘要](#a2-请求摘要)
  - [A.3 Manifest 摘要](#a3-manifest-摘要)
  - [A.4 固定校验向量与一致性要求](#a4-固定校验向量与一致性要求)

## 1. 范围与路由

本协议定义 A-BOX 与业务平台之间的公共通信契约。业务 Profile 定义命令参数、操作类别、`200` 成功边界、结果、报告、实例、状态枚举及业务安全语义；相同能力必须复用相同语义，并遵循公共框架。

**表 1：公共消息路由**

| 对象 | 权威规则 |
|---|---|
| 设备身份 | 每台物理 A-BOX 由平台分配全局唯一 `deviceId`，用于 Topic，不要求在每条 Payload 中重复 |
| Request 路由 | `command + params`；执行前必须唯一确定目标操作 |
| Report 路由 | 非心跳主动业务消息统一进入 `/report`，按 `reportType + reportName` 路由；业务数据放在 `payload` |
| Response 归属 | Topic 中的 `deviceId` 与 `data.requestId` 关联原请求及其目标设备；不得将其他设备或未关联请求的响应用于完成当前操作 |
| Heartbeat 归属 | 只更新 Topic 中该 `deviceId` 的通信在线状态；业务能力是否已确认按第 7.3 节判断，不能将一次心跳应用到其他设备 |
| Profile | 能力、版本及业务语义的定义单位；一台设备可实现多个，不要求每条 Request/Report 携带 Profile 标识；Manifest 不参与逐条消息路由 |
| 单实例 | 不要求实例标识 |
| 多实例 | Report 使用 `instanceId`，请求目标字段由 Profile 在 `params` 中定义；具体约束见第 5.3 节 |

产品协议须明确适用的 Profile、版本及业务报告。平台按 Manifest 中的 Profile 名称和版本识别能力，并按第 7.3 节确认；一台设备可同时接入多个 Profile。身份、心跳、Manifest、状态及请求记录按 Topic 的 `deviceId` 隔离，消息按表 1 路由。

公共业务与 `/internal/*` 运维接口分离，允许离线恢复的运维业务不得与实时控制混用。

### 1.1 按任务阅读

| 接入任务 | 建议阅读顺序 | 本步要明确的结果 |
| --- | --- | --- |
| 建立订阅、发现设备 | [2.1 Topic](#21-topicqosretain) → [2.2 会话](#22-会话与自动重连) → [2.3 身份](#23-身份clientid-与-acl) | 能按 Topic 的 `deviceId` 区分各设备消息，并先订阅后发请求 |
| 确认当前可用能力 | [7.2 Manifest](#72-manifestdata-schema) → [7.3 在线确认](#73-摘要要求发布与在线确认) → [7.4 版本兼容](#74-profile-版本兼容) | 同一设备的在线心跳与能力声明匹配，目标 Profile 版本可用 |
| 恢复业务状态 | [第 6 节](#6-完整状态与-sync_state) → 产品的完整状态定义 → [5.4 有序流](#54-严格有序流)（适用时） | 按本轮 `expectedStates` 收齐关联报告，另行维护最新状态及新鲜度 |
| 发命令并判断结果 | [第 3 节](#3-公共-json-与字段限制) → [第 4 节](#4-requestresponse-与执行规则) → 产品命令 | 请求合法、有效，响应归属明确，按该操作的成功边界解释结果 |
| 处理丢失、重复及恢复 | [4.2～4.4](#42-操作类别与重试) → [5.2～5.4](#52-reportid-与去重) → [6.4](#64-重试乱序与超时) → 产品超时与退出规则 | 不把传输确认、旧消息或不完整状态用作当前动作成功依据 |
| 实现规范化与摘要 | [3.2 数值规则](#32-数值扩展及非法报文) → [规范性附录 A](#附录-a规范性canonicalization-与摘要) | 请求内容一致性、Manifest 摘要及固定向量遵循同一规则 |

### 1.2 关键概念对照

| 概念或字段 | 解决的问题 | 容易混淆的边界与定义位置 |
| --- | --- | --- |
| 文档修订号、外层 `version`、Profile 版本、`firmware` | 分别标识规范修订、公共报文合同、业务能力合同和设备固件 | 四者不互相替代；报文兼容见[第 9 节](#9-平台处理与-v4-兼容性)，Profile 兼容见[7.4](#74-profile-版本兼容) |
| Topic 中的 `deviceId` | 确定消息所属设备 | 是请求关联和状态隔离的前提，不能从订阅连接猜测归属；见本节路由表和[2.3](#23-身份clientid-与-acl) |
| Manifest 与 Heartbeat | 分别声明能力和证明通信存活 | Retained 声明不证明当前在线，心跳也不单独证明业务能力或安全可用；见[7.3](#73-摘要要求发布与在线确认) |
| `requestId` | 关联原请求、响应及有因果关系的报告 | 同 ID 重试还须满足内容一致性和操作类别规则；不会使每份后续报告都成为该请求的回执；见[4.2～4.4](#42-操作类别与重试)、[5.1](#51-reportdata-schema) |
| `reportId` | 识别同一份报告并去重 | 新逻辑报告使用新 ID；重复报告不能当成新状态；见[5.2](#52-reportid-与去重) |
| 报告的 `streamId` 与 `sequence` | 判断同一有序报告流内的先后 | 不跨流比较序号；不是产品控制请求的会话或控制序号；见[5.4](#54-严格有序流) |
| `sync_state` 的 `expectedStates` | 确定本轮应收齐的完整状态集合 | `200` 只确认同步已安排，普通自动报告不能补齐本轮；见[6.2～6.4](#62-core-sync_state) |
| Response 的 `200` 与实际动作完成 | 表达当前操作已达到何种成功边界 | 边界由 Profile 逐项规定，不能从 MQTT PUBACK 或任意 `200` 推导机械完成；见[4.5](#45-responsedata-schema-与成功边界) |

### 1.3 时间边界对照

公共时间窗口如下；产品的控制租约、响应等待和状态新鲜度仍按对应 Profile 执行。

| 时间边界 | 起点或比较依据 | 数值或规则来源 | 不能据此推导的结果 |
| --- | --- | --- | --- |
| 新请求有效期 | 原始 `timestamp` 与 `expiresAt` 确定允许时长，接受时再与可信当前 UTC 比较 | [4.3](#43-有效期与校验顺序)及目标 Profile 的 `maxValidity` | 请求被接受后的持续时间或机械完成时间 |
| 离散动作幂等保护 | 有 `expiresAt` 的请求，记录至少保留至 `expiresAt + 10 分钟` | [4.4](#44-请求一致性与保护窗口) | 窗口内允许重复执行动作 |
| Report 去重记录 | 按 `deviceId + reportId` 识别已处理报告 | 固定 24 小时，见[5.2](#52-reportid-与去重) | 状态在这段时间内始终新鲜 |
| 本轮同步报告等待 | 从收到同步成功响应起 | 固定 15 秒，见[6.4](#64-重试乱序与超时)；报告可先于响应到达 | 超时就等于设备离线、故障或已经停止 |
| 公共通信在线判定 | 按[7.1](#71-heartbeatdata-schema)规定的心跳接收时间口径 | 心跳周期 5 秒，连续 15 秒未收到即离线 | 业务反馈新鲜、控制租约有效或车辆安全可控 |

## 2. Topic、传输与会话

### 2.1 Topic、QoS、Retain

**表 2：Topic、QoS 与 Retain**

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

**表 3：MQTT 会话必需设置**

| MQTT 版本 | A-BOX 公共业务客户端必需设置 |
|---|---|
| 3.1.1 | `Clean Session = true` |
| 5.0 | 同时设置 `Clean Start = true`、`Session Expiry Interval = 0` |

上述会话要求只针对 A-BOX 公共业务客户端；平台消费者、Bridge 等服务端组件的持久会话由部署架构决定。

**表 4：自动重连阶段与时序**

| 自动重连阶段 | 固定规则 |
|---|---|
| 首次连接 | 随机延迟 0～4 秒 |
| 连续失败 | 退避上限依次为 4、8、16、32、60 秒，后续保持 60 秒 |
| 实际延迟 | 每次从当前上限 50%～100% 取带设备差异的随机抖动，避免设备同步重连 |
| 重置 | 稳定在线 5 分钟后重置退避级别 |

A-BOX 必须自动重连。成功重连后的逻辑发送顺序为 **Manifest → 启动/恢复 Heartbeat → 补发所有完整 State → 正常业务**。该顺序约束设备发送逻辑；平台不得假定不同 Topic 经中间链路仍按此顺序到达。能力确认前到达的 Report 可以保存或缓存，但不能据此确认新能力可用。

### 2.3 身份、ClientId 与 ACL

**表 5：身份、ClientId 与 ACL**

| 项目 | 约束 |
|---|---|
| `deviceId` | 1～32 个 ASCII 字符，完整匹配 `^[A-Za-z0-9_-]+$`；禁止 `/`、`+`、`#`、空白和控制字符 |
| MQTT `ClientId` | 必须逐字等于 `deviceId` |
| 凭据 | 每设备独立；禁止多台生产设备共享同一可写公共业务凭据 |
| 服务端绑定 | 已认证设备身份唯一绑定一个 `deviceId`；Broker 绑定凭据、ClientId、Topic ACL，不得仅信任客户端声明的身份或 ClientId |
| 发布权限 | 只能发布自身 `response`、`report`、`heartbeat`、`manifest` |
| 订阅权限 | 只能订阅自身 `request` |

ACL 必须由服务端身份绑定生成；禁止跨设备发布/订阅、用通配符扩大设备授权或构造 Topic 绕过绑定。

V4 设备必须使用 MQTT over TLS，最低 TLS 1.2。证书认证必须校验证书链、主机名、有效时间并发送正确 SNI；PSK 认证使用设备独立的身份和密钥。身份、独立凭据与 ACL 要求相同。凭据轮换通过受控配置或生产 provisioning，不能走公共业务 Topic；切换后必须撤销旧凭据。

## 3. 公共 JSON 与字段限制

### 3.1 外层 Schema

所有 Payload 使用 UTF-8 JSON，禁止 BOM；固定 Topic 段使用小写。

**表 6：公共外层 Schema**

| 字段路径 | 类型 | 必填或出现条件 | 约束与说明 |
|---|---|---|---|
| `version` | String | 是 | 固定 `"4.0"` |
| `timestamp` | Integer | 是 | 消息生成时间，UTC Unix Epoch 毫秒 |
| `data` | Object | 是 | 对应消息内容 |

**示例 1：外层 Schema（双向；公共信封结构示意，非独立接口报文）**

```json
{
  "version": "4.0",
  "timestamp": 1770186088333,
  "data": {}
}
```

### 3.2 数值、扩展及非法报文

**表 7：数值类型与词法**

| 项目 | 规则 |
|---|---|
| `Integer` 字段 | 仅允许 `-9007199254740991..9007199254740991` 的安全整数，并满足字段自身范围；使用十进制整数词法，禁止小数、指数形式、前导 `+`、多余前导零和负零 |
| 业务 `Number` 字段 | `params`、`result`、`payload` 中由 Profile 明确定义为 Number 的字段允许整数、小数和 JSON 指数形式；数值按 IEEE 754 binary64（双精度）的舍入到最近值、相同距离取偶数规则解释，且必须满足 Profile 的范围及分辨率要求 |
| 公共数值字段 | `timestamp`、`expiresAt`、`code`、`sequence`、计数等保持各自 Integer 定义；不得因支持业务小数改成 Number、字符串或改变单位 |
| 非法数字 | NaN、正负无穷、负零（含 `-0.0`、`-0e0`）及转换为 binary64 时溢出或非零下溢为零的数字均非法 |
| Epoch 毫秒 | `timestamp`、`expiresAt` 及未来 Epoch 字段具有 64 位整数语义；必须输出非负十进制整数，不接受字符串、小数、指数、前导 `+` 或负零；不得因浮点转换截断、舍入或输出科学计数法 |

**表 8：重复 Key、扩展字段与枚举**

| 项目 | 规则 |
|---|---|
| 重复 JSON Key | 任意 Object 重复 Key 均非法，必须拒绝，不能选择其中一个值 |
| 未知字段 | 必须忽略可安全忽略的扩展字段，不能仅因未知可选字段丢弃合法消息 |
| 未知业务枚举 | 必须保留原值，可降级展示 `unknown`；不得解释为成功、完成、停止、关闭、正常或其他安全状态 |

Profile 必须为每个 Number 字段定义单位、范围、分辨率及无效值表达；未另行规定时，超出范围或分辨率的请求值须拒绝，不得静默舍入或截断后执行。分辨率按数值判断，不要求固定小数位数；`1.5` 与 `1.500` 表示相同数值。Number 不提供任意精度十进制保证；需要精确整数的字段仍使用 Integer。

业务小数能力以已确认的 Profile 版本及其字段定义为准；仅有 `version="4.0"` 不表示任意业务字段都接受小数。不得向未明确支持该 Number 字段的设备下发小数参数。数值规范化、整数边界及非法词法向量见[规范性附录 A](#a1-v4-json-canonicalization)。

### 3.3 长度与大小

**表 9：字段长度与报文大小**

| 项目 | 上限或格式 |
|---|---|
| 单条 MQTT Payload | 完整 UTF-8 JSON 最大 1024 字节，不含 Topic 和 MQTT 包头 |
| `params` | 序列化后最大 768 字节，仍受整包上限限制 |
| `result` | 序列化后最大 768 字节，仍受整包上限限制 |
| `payload` | 序列化后最大 768 字节，仍受整包上限限制 |
| `requestId` | 1～64 个 ASCII 字符，`^[A-Za-z0-9][A-Za-z0-9._:-]*$` |
| `reportId` | 1～96 个 ASCII 字符，同上字符表达式 |
| `streamId` | 1～64 个 ASCII 字符，同上字符表达式 |
| `instanceId` | 1～32 个 ASCII 字符，`^[A-Za-z0-9_-]+$` |
| `command` | 最长 64 个字符，小写 snake_case；命名规则见第 8 节 |
| `reportName` | 最长 64 个字符，小写 snake_case；命名规则见第 8 节 |
| Profile 名称 | 最长 64 个字符，小写 snake_case；命名规则见第 8 节 |
| `firmware` | 最长 64 个 ASCII 字符 |
| `msg` | 最长 128 个 UTF-8 字节，禁止控制字符 |
| Manifest | 最多 16 个 Profile |

超限数据不得在公共 Topic 中自行分片。

## 4. Request、Response 与执行规则

### 4.1 Request.data Schema

**表 10：Request.data 字段**

| 字段路径 | 类型 | 必填或出现条件 | 约束与说明 |
|---|---|---|---|
| `requestId` | String | 是 | 平台生成的全局唯一请求标识 |
| `command` | String | 是 | 业务命令 |
| `expiresAt` | Integer | 按操作类别 | UTC Epoch 毫秒失效时间 |
| `params` | Object / null | 是 | 业务参数，无参数时显式 `null` |

**示例 2：Request.data Schema（平台 → 设备；完整请求）**

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

**`QUERY`**

接受与安全规则：不应产生不可逆物理副作用；`expiresAt` 默认可选；`sync_state` 属于 QUERY

相同 requestId、相同内容的重试：允许重新执行及重新生成同步结果；新 Report 使用新 reportId，关联原 requestId

**`DISCRETE_ACTION`**

接受与安全规则：expiresAt 必填；Profile 定义最大有效期；必须幂等，MQTT 重投不得重复动作，必须具备跨重启保护

相同 requestId、相同内容的重试：不得重复动作；应返回首次完整响应；持久化异步最终结果的策略由 Profile 定义

**`CONTINUOUS_SESSION`**

接受与安全规则：Profile 至少定义等价的 sessionId、sequence、lease/timeout 机制；会话失效、断网、租约超时、序号异常进入规定安全状态

相同 requestId、相同内容的重试：按 Profile 会话、序号、租约规则

**`SAFETY_STOP`**

接受与安全规则：由 Profile 定义；应天然幂等，可有特殊过期规则，可重复执行或绕过普通 Busy；不得绕过认证、基本合法性及必要参数校验

相同 requestId、相同内容的重试：按 Profile 安全规则，可允许重复执行

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

**表 11：Response.data 字段**

| 字段路径 | 类型 | 必填或出现条件 | 约束与说明 |
|---|---|---|---|
| `requestId` | String | 是 | 原请求标识 |
| `code` | Integer | 是 | 请求处理结果 |
| `msg` | String | 是 | 展示与诊断文本 |
| `result` | Object | 否 | 同步结构化结果；无结果时省略，不发送 `null` |

**示例 3：Response.data Schema 与成功边界（设备 → 平台；完整响应）**

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

`200` 表示达到 Profile 对当前操作定义的成功边界；Profile 必须逐项明确该边界，不统一表示机械完成。

### 4.6 公共响应码与异常响应

**表 12：公共响应码**

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

**表 13：Report.data 字段**

| 字段路径 | 类型 | 必填或出现条件 | 约束与说明 |
|---|---|---|---|
| `reportId` | String | 是 | 本次逻辑报告唯一标识 |
| `reportType` | String | 是 | state / event / telemetry |
| `reportName` | String | 是 | 业务上报名称 |
| `instanceId` | String | 多实例时 | 业务实例 |
| `streamId` | String | 严格有序流时 | 流标识，与 sequence 成对 |
| `sequence` | Integer | 严格有序流时 | 流内序号，与 streamId 成对 |
| `requestId` | String | 关联请求时 | 请求的异步后续 Report 必须携带原标识 |
| `payload` | Object / null | 是 | 业务数据；完成、失败、停止语义由 Profile 定义 |

**表 14：reportType 枚举**

| reportType | 语义 |
|---|---|
| `state` | 当前业务状态或完整状态快照 |
| `event` | 一次离散业务事件 |
| `telemetry` | 连续、高频、允许少量丢失的数据 |

Profile 不得增加 reportType；新业务语义使用新 reportName。QoS/Retain 只在第 2.1 节定义。

**示例 4：Report.data Schema（设备 → 平台；完整报告）**

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

**示例 5：Report.data Schema（设备 → 平台；完整报告）**

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

普通 state 按平台接收顺序维护最新快照，先按 `deviceId + reportId` 执行 24 小时去重；同一逻辑报告重投不能替换状态或刷新其接收时间。state 的 Retain 必须为 false，保留消息不作为实时状态。`timestamp` 用于观测设备时间，不能作为跨重启、校时或未知 UTC 时的唯一新旧依据；相同或回退的设备时间不能使新的普通状态永久失去更新资格。离线时长及展示的新鲜度从该逻辑报告首次被平台接收的时间起算，心跳不能刷新状态或实测反馈的新鲜度。

普通 Report 不提供生成顺序保证：无序流中晚到的新 reportId 按接收顺序处理，平台不得声称它一定比先到报文更晚生成。需要拒绝同一流内的迟到状态时，Profile 应使用已有的严格有序流。重连后按第 6 节接收完整状态，并可重新发起 `sync_state`；历史状态须保留接收时间及过期标识，不能因设备重新在线而自动变为新鲜。

### 5.4 严格有序流

普通 Report 不要求序号；需严格顺序的 Profile 使用 streamId + sequence：

1. 两字段同时出现或同时不存在；sequence 为 uint32，从 1 递增。
2. uint32 回绕前更换 streamId；重启或流重新初始化必须生成新 streamId。
3. 仅在相同 `deviceId + reportName + instanceId + streamId` 内比较 sequence；小于或等于已接受序号的报告不更新最新状态，也不刷新新鲜度，即使其 reportId 不同。不同 streamId 序号不能直接比较新旧；流切换按接收顺序建立当前快照，不能仅凭较大序号认定报文生成较晚。
4. 不同 streamId 之间的严格生成顺序不由本协议保证。若 Profile 需要跨流防迟到，须另行定义流切换依据；不能从设备 UTC 或序号大小推断重启先后。

## 6. 完整状态与 sync_state

### 6.1 完整状态及自动补发

每个有状态 Profile 必须登记完整状态 reportName；完整状态须无需历史消息即可重建当前可观察状态。设备启动完成且业务获得可靠状态后，以及 MQTT 重连后，必须补发全部当前启用 Profile 的完整状态；自动补发省略 requestId。平台持久保存最新状态并可发起 sync_state。

### 6.2 Core sync_state

**表 15：Core sync_state 接口属性**

| 项目 | 定义 |
|---|---|
| command / category | `sync_state` / `QUERY`，Core 保留命令 |
| params | `null` |
| `200` | 已接受并安排同步，不代表 Report 全部发布、Broker 或后续平台链路收到并处理 |
| result.expectedStates | 本轮权威预期状态集合 |

**示例 6：Core sync_state（平台 → 设备；完整请求）**

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

**示例 7：Core sync_state（设备 → 平台；完整响应）**

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

同步轮次与最新状态是两种独立判断：`requestId` 只关联对应轮次，不能用旧轮次报告补齐新轮次的 expectedStates。已超时、结束或废弃轮次的迟到 Response/Report 不得使该轮重新成功，也不得解除新轮次的等待；有效 state 是否更新最新快照仍按第 5.3、5.4 节判断。同一轮内的严格有序状态仍须先通过流内序号检查；“最后接收”不覆盖去重和严格有序规则。

### 6.4 重试、乱序与超时

**同 requestId、同内容重试**

沿用首次 expectedStates，重新发送集合中完整状态；新 Report 用新 reportId 并关联原 requestId；不得因当前 Manifest/能力变化改变集合

**同步中 Manifest 变化**

平台必须废弃未完成旧轮次，等待新 Manifest 在线确认后以新 requestId 同步；设备建立新集合

**Report 先于 Response 到达**

平台应按 requestId 临时缓存完整状态，收到响应后按 expectedStates 匹配；不得仅因响应晚到丢弃合法结果

**等待超时**

从收到成功响应起固定 15 秒；仅代表未收齐，不能推断设备离线、故障、停止、关闭、正常或其他确定状态；可保留旧持久状态，标为不完整或未知新鲜度

## 7. Heartbeat、Manifest 与能力确认

### 7.1 Heartbeat.data Schema

**表 16：Heartbeat.data 字段**

| 字段路径 | 类型 | 必填或出现条件 | 约束与说明 |
|---|---|---|---|
| `firmware` | String | 是 | 当前固件版本字符串 |
| `manifestId` | String | 条件必填 | 第 7.3 节要求绑定能力时回显 |

**示例 8：Heartbeat.data Schema（设备 → 平台；完整心跳）**

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

**表 17：Manifest.data 字段**

| 字段路径 | 类型 | 必填或出现条件 | 约束与说明 |
|---|---|---|---|
| `manifestId` | String | 条件必填 | 规范化内容摘要，见第 7.3 节及附录 A |
| `firmware` | String | 是 | 当前固件版本 |
| `profiles` | Array | 是 | 当前实际启用能力 |
| `profiles[].name` | String | 是 | Profile 名称，集合内必须唯一 |
| `profiles[].version` | String | 是 | MAJOR.MINOR |
| `profiles[].instances` | Array&lt;String&gt; | 多实例时 | 完整实例集合，标识唯一；单实例必须省略 |

**示例 9：Manifest.data Schema（设备 → 平台；完整 Manifest 示例，含占位值）**

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

Manifest 由设备主动发布，平台订阅第 2.1 节的 Manifest Topic 获取。Broker 已保存 Retained Manifest 时，即使设备离线，新订阅者也会收到最近声明。`sync_state` 仅同步完整业务状态，不能用于获取或刷新 Manifest。

平台未收到 Manifest 时保持能力未确认；收到后按表 18 与同一设备的当前在线心跳匹配。

**表 18：在线实例能力确认条件**

| 模式 | 当前在线实例能力确认条件 |
|---|---|
| 带 manifestId | 当前在线心跳与 Retained Manifest 的 manifestId 相同 |
| 省略 manifestId | 满足 firmware 唯一决定能力的前提，且当前在线心跳 firmware 与 Retained Manifest 完全一致 |

下发动作前必须收到报文版本为 `4.0` 的当前在线心跳并按表 18 确认 Manifest；版本或能力不明确时不得下发，确认前不得标记能力可用。平台只应向支持目标 Profile、平台支持其 MAJOR 且操作在设备版本能力内的设备发动作；设备仍必须执行有效期、幂等、租约及 Profile 安全保护。

### 7.4 Profile 版本兼容

MAJOR、MINOR 均为 `0..65535` 的 uint16 十进制数，无前导零，格式 MAJOR.MINOR。

**表 19：Profile 版本兼容规则**

| 变化或组合 | 规则 |
|---|---|
| MAJOR 变化 | 不兼容变化：修改字段语义/必填字段、删除能力、修改成功边界或 Report 基本语义 |
| MINOR 变化 | 只允许兼容扩展：新增 command/reportName、可忽略可选字段、专用响应码 |
| 平台不支持 MAJOR | 不得发动作；可保存未知 Report 原始数据，不得确认可控 |
| 相同 MAJOR，设备 MINOR 较高 | 使用已知兼容子集，忽略未知兼容扩展 |
| 设备 MINOR 较低 | 不得使用更高 MINOR 引入的能力 |

### 7.5 平台首次接入与恢复流程

1. **建立订阅。** 按第 2.1 节先订阅目标设备的 Manifest、Heartbeat、Report 和 Response，再发送请求；多设备可统一订阅。
2. **识别设备与能力。** 从 Topic 提取 `deviceId` 分别维护设备记录；Manifest 按 Profile 名称查找，版本按第 7.4 节匹配。
3. **确认当前能力。** 将同一设备的 Manifest 与当前在线心跳按第 7.3 节匹配。
4. **恢复状态。** 接收启动、重连后的完整状态，按需发起 `sync_state`；本轮同步完成按第 6.3 节判定，最新状态另按第 5.3、5.4 节维护。
5. **发送操作并关联结果。** 保存目标 `deviceId`、`requestId` 及原请求，仅用同设备关联的 Response 判断请求结果；成功边界由对应 Profile 定义。
6. **处理变化与不确定结果。** 按第 2、4～7 节处理离线、能力变化、重试、旧消息和同步超时。未知 Profile 或报告可保存原始数据，不套用其他产品语义。

## 8. Registry 与 Profile 登记要求

Command、ReportName、Profile 必须统一登记，登记信息须可被机器校验，并满足本节的命名、唯一性和路由要求。

V4.0 公共 Core 保留命令为 `sync_state`，接口定义见[第 6.2 节](#62-core-sync_state)。

**表 20：Registry 命名与唯一性**

| 对象 | 命名与唯一性 |
|---|---|
| command | 每个名称只登记一项全局能力语义；多个 Profile 可引用，不能同名异义 |
| reportName | 全局唯一，不能同名异义 |
| Core 命令 | sync_state 等保留名称不得被 Profile 覆盖或重释 |
| 命名 | 小写、下划线连接；不得用车型/客户/项目名作业务前缀；相同语义复用名称；仅产品独有能力使用产品前缀 |

command 标识能力；各 Profile 可定义其目标、成功边界、结果及状态，但须保持全局语义。`power_on` / `power_off` 表示请求电源控制输出进入开启/关闭目标，不保证负载通断或授予运动权限。

同设备引用同名命令的多个 Profile 必须由 command + params 唯一确定目标，否则不得同时启用；不得有路由冲突。多个 operationMatcher 必须且只能命中一个；0 个或多个均返回 4001，禁止动作，不得靠匹配顺序消歧。整个 command 所有参数组合类别一致时可省略 operationMatcher。

**表 21：Registry 最小登记字段**

| Registry | 最小登记字段 |
|---|---|
| Command | command、introducedVersion、semanticDescription、applicableProfiles |
| ReportName | reportName、ownerProfile、introducedVersion、semanticDescription |
| Profile | profileName、supportedMajorVersions、currentVersion |

**表 22：Profile 操作登记字段**

| Profile 操作登记字段 | 定义 |
|---|---|
| command | 名称 |
| operationMatcher | 参数匹配条件 |
| category | 四种操作类别之一 |
| params | 参数 Schema |
| expiresAt | 必填/可选/特殊规则 |
| maxValidity | 最大有效期 |
| 200Boundary | 精确成功边界 |
| result | 同步结果 |
| rebootPolicy | 跨重启策略 |
| errorCodes | 专用码 |

**表 23：Profile 报告登记字段**

| Profile Report 登记字段 | 定义 |
|---|---|
| reportName | 名称 |
| reportType | 三种类型之一 |
| payload | Schema |
| fullState | 是否完整状态 |
| instanceId | 是否必需；多实例须与 Manifest instances 一致 |
| orderedStream | 是否需要 streamId + sequence |
| requestCorrelation | 是否可能关联 requestId |

## 9. 平台处理与 V4 兼容性

平台必须先校验公共外层，再按公共路由处理；去重、最新状态主键、乱序、在线判定、未知字段/枚举分别遵循第 3、5～7 节。

**表 24：公共协议版本兼容规则**

| 变化 | 版本要求 |
|---|---|
| 新 Profile、command、reportName；Profile MINOR 扩展；params/payload 可忽略可选字段；专用响应码；Manifest 兼容可选字段 | 可继续 `version="4.0"` |
| Profile 明确定义的业务 Number 字段及其小数编码 | 可继续 `version="4.0"`，须按已确认的 Profile 版本使用；不得将既有 Integer 字段直接改为 Number 或改变既有字段单位、范围、成功边界 |
| 公共字段名、类型、必填性或既有语义；五 Topic 基本资源模型；requestId 基本幂等；reportId 去重；四类操作或 reportType 基本语义；会话安全边界 | 必须升级公共协议主版本 |

业务 Number 的编码规则不改变既有 Integer 的合法输入、规范化字节或摘要。改变既有 Profile 字段类型、单位或其他语义仍按第 7.4 节处理；平台和设备只可使用双方支持的 Profile 合同。

## 附录 A（规范性）：Canonicalization 与摘要

### A.1 V4 JSON Canonicalization

固定采用 **语义集合归一化 + RFC 8785 JCS**。本协议使用的规范化规则如下：

1. 解析时拒绝重复 Key、非法数字及不符合字段合同的值。Key 在 JSON 解码后判重，例如 `"a"` 与 `"\u0061"` 是同一个 Key；非法 Unicode（含孤立代理项）不得进入规范化。
2. Manifest profiles 先按 name 的 Unicode 码点顺序稳定排序；instances 按实例标识 Unicode 码点顺序稳定排序，重复实例非法。
3. 其他数组保持顺序，只有 Core/Profile 明定为集合时可先排序。
4. 随后执行 RFC 8785 JCS：所有对象（包括数组中的对象）的 Key 递归按解码后字符串的无符号 UTF-16 码元序列升序排列；从首个不同码元比较，同前缀时短字符串在前，不使用区域设置或转义文本排序。此步骤不重排数组。
5. 不执行 Unicode Normalization；null 保留为 JSON null。字符串比较基于解码后的 Unicode 字符值，统一 UTF-8 Canonicalization。
6. 数字先按第 3.2 节及字段合同校验；Integer 输出最短十进制整数，Number 使用下述 JCS 数字规则。不得将 Number 转成字符串、按显示位数舍入、按业务容差合并不同值，或将非法输入转换成合法值后计算摘要。

JCS 数字序列化按以下规则确定，字段分辨率不改变该规则：

**表 25：数值 Canonicalization 规则**

| 数字项 | Canonicalization 规则 |
|---|---|
| 数值模型 | Number 的十进制输入先按第 3.2 节转换为 binary64；同一 binary64 值必须使用同一文本，不保留原始小数位数或指数写法 |
| 最短有效数字 | 对非零绝对值，选择读回为同一 binary64 值的最少位数十进制有效数字；多个同位数候选取最接近该 binary64 值者，距离相同取十进制尾数为偶数者 |
| 普通十进制 | 绝对值满足 `1e-6 <= abs(value) < 1e21` 时使用普通十进制；小于 1 时保留整数位 `0`，不补末尾小数零或无意义小数点 |
| 科学计数 | 其余非零值使用一个非零整数位及必要的小数位，随后为小写 `e` 和十进制指数；正指数必须带 `+`，负指数带 `-`，指数不含多余前导零，例如 `1e-7`、`1e+21` |
| 符号和零 | 负数加前导 `-`，正数无前导 `+`；零只输出 `0`。第 3.2 节禁止的负零须先拒绝，不得规范化为合法零 |

例如 Number 输入 `1`、`1.0`、`1e0` 均输出 `1`；`1.5`、`1.500`、`15e-1` 均输出 `1.5`。这些等价写法不适用于 Integer 字段的词法校验，`timestamp:1.0` 仍非法。

**表 26：JSON 序列化固定规则**

| 序列化项 | 固定规则 |
|---|---|
| 对象与数组 | 使用 JSON 的括号、逗号和冒号；不输出 Token 间空白或末尾换行，字符串内部空白保留 |
| 字符串 | 使用双引号；`"`、`\` 分别转义为 `\"`、`\\`；U+0008/0009/000A/000C/000D 分别使用 `\b`、`\t`、`\n`、`\f`、`\r`，其余 U+0000..001F 使用小写十六进制 `\u00hh`；其他字符直接输出，不额外转义 `/` 或非 ASCII 字符 |
| 字面量 | 只输出 `true`、`false`、`null`，不能省略对象中的 null 值 |
| Canonical Bytes | 将上述完整序列化结果编码为 UTF-8，不加 BOM、字符串终止符或额外换行 |

上述序列化细则展开自既定的 [RFC 8785 JCS](https://www.rfc-editor.org/rfc/rfc8785.html)；V4 的数字范围和词法限制仍以第 3.2 节为准。

对象字段乱序且值相同须产生相同 Canonical Bytes；普通 `[1,2]` 与 `[2,1]` 不同。Manifest Profile 顺序或 instances 顺序不同但集合相同须摘要一致；重复实例不能进入摘要。

### A.2 请求摘要

持久化或计算摘要时，必须按 A.1 规范化逻辑对象：

**示例 10：请求摘要（摘要输入或校验片段；精确文本，不参与传输）**

```json
{"command":"...","params":null,"expiresAt":1770186093333}
```

command 和 params 必须参与；携带 expiresAt 时必须参与。不使用 expiresAt 时须省略，不能补 null。缺 params 非法，params:null 与 params:{} 不等价。业务集合先按 Profile 声明归一化。

固定 `Request Digest = lowercase_hex(SHA-256(Canonical Bytes))`：对完整 Canonical Bytes 计算 SHA-256，将 32 字节结果按字节顺序输出为 64 个小写十六进制字符，不加前缀，不得选其他算法；外层 timestamp 不参与。

### A.3 Manifest 摘要

摘要输入为 Manifest data 除 manifestId 自身外的**全部字段**；V4 为 `{"firmware":"...","profiles":[...]}`。未来兼容新增字段默认参与，除非公共协议明确声明不参与。外层 version/timestamp 不参与。

固定 `manifestId = "sha256:" + lowercase_hex(SHA-256(canonical_manifest_data))`。SHA-256 的输入为按 A.1 得到的完整 Canonical Bytes，结果编码与 A.2 相同，再加字面前缀 `sha256:`；不得选其他算法。摘要自身及该前缀不参与本次摘要输入。

### A.4 固定校验向量与一致性要求

设备与平台须按 A.1～A.3 生成一致的 Canonical Bytes 和摘要。下列 Canonical JSON 为精确原始文本，其 UTF-8 编码作为摘要输入，不包含 Markdown 反引号或显示换行。

Manifest 示例的归一化前摘要输入为：

**示例 11：固定校验向量与一致性要求（摘要输入或校验片段；精确文本，不参与传输）**

```json
{"profiles":[{"instances":["2","1"],"version":"1.1","name":"example_b"},{"version":"1.0","name":"example_a"}],"firmware":"test-1.0.0"}
```

#### A.4.1 Manifest 集合归一化

Canonical JSON（精确摘要输入；无末尾换行）：

```text
{"firmware":"test-1.0.0","profiles":[{"name":"example_a","version":"1.0"},{"instances":["1","2"],"name":"example_b","version":"1.1"}]}
```

SHA-256（小写十六进制）：

```text
c59a64516ec2b9a7baa3695d65783d375000b6d323c4446e7df9abe6e144bda3
```

#### A.4.2 Unicode 原文编码

Canonical JSON（精确摘要输入；无末尾换行）：

```text
{"msg":"格口打开"}
```

SHA-256（小写十六进制）：

```text
da2ac1209441101787426bfdc0516d6b2b5a6dddb621a2d4c2b036bf7f9ac6bd
```

#### A.4.3 普通数组，顺序一

Canonical JSON（精确摘要输入；无末尾换行）：

```text
{"values":[1,2,3]}
```

SHA-256（小写十六进制）：

```text
abe0a9abcc957f9f5aa04d68c4f49fadee7c4b62343e403a3a1c0b646519d53c
```

#### A.4.4 普通数组，顺序二

Canonical JSON（精确摘要输入；无末尾换行）：

```text
{"values":[3,2,1]}
```

SHA-256（小写十六进制）：

```text
9292645f8764edcacb2ee3329165d4fb46edb38160866aaa35850bfd5333d384
```

#### A.4.5 最大安全整数

Canonical JSON（精确摘要输入；无末尾换行）：

```text
{"timestamp":9007199254740991}
```

SHA-256（小写十六进制）：

```text
cc8af82fdaab301cdbc3ccf2ed87f8401eeee55d745544f3c31b813e487828ca
```

#### A.4.6 请求逻辑对象，无 expiresAt

Canonical JSON（精确摘要输入；无末尾换行）：

```text
{"command":"sync_state","params":null}
```

SHA-256（小写十六进制）：

```text
c51070fb537c79f32338f8511dbd8538d4e4c2f9b50cf6f362c54c87f4c7322f
```

#### A.4.7 请求逻辑对象，params 为 null

Canonical JSON（精确摘要输入；无末尾换行）：

```text
{"command":"example_action","expiresAt":1770186093333,"params":null}
```

SHA-256（小写十六进制）：

```text
fda1437572c08f464d1fba64c7aa39668ac9e5349698108a4ce9813e12adb124
```

#### A.4.8 请求逻辑对象，params 为对象

Canonical JSON（精确摘要输入；无末尾换行）：

```text
{"command":"example_action","expiresAt":1770186093333,"params":{}}
```

SHA-256（小写十六进制）：

```text
b59db125242c3ccc0a9b5d298ddde8793c3ca89f056ad3607d304d08ff2dbb58
```

#### A.4.9 Number 小数

Canonical JSON（精确摘要输入；无末尾换行）：

```text
{"value":1.5}
```

SHA-256（小写十六进制）：

```text
45ad93d73171ea0139bdcfebb9eb613c2a5ae9aa299bf98faa4592b2446e1d87
```

#### A.4.10 Number 整数值

Canonical JSON（精确摘要输入；无末尾换行）：

```text
{"value":1}
```

SHA-256（小写十六进制）：

```text
48208f9428d64634bd8e28ff345bf0eab60d53c18fa2fbdb0b9bc1e84df2b5f6
```

#### A.4.11 Number 普通十进制边界

Canonical JSON（精确摘要输入；无末尾换行）：

```text
{"value":0.000001}
```

SHA-256（小写十六进制）：

```text
bc1c982dd4e677d72ff5821cc67554e045e838a31ddc55fa0fdb1a32d5a1dce9
```

#### A.4.12 Number 负指数

Canonical JSON（精确摘要输入；无末尾换行）：

```text
{"value":1e-7}
```

SHA-256（小写十六进制）：

```text
910750038eb30a40d06da88009b6259aabaf3751b3cb212c616d51abb6b008a4
```

#### A.4.13 Number 正指数

Canonical JSON（精确摘要输入；无末尾换行）：

```text
{"value":1e+21}
```

SHA-256（小写十六进制）：

```text
776bf0d292f3767fdfca3fdc9683d0ba51886b8e6e79cd228a41bafcfa9a1a6f
```

#### A.4.14 Number binary64 最短表示

Canonical JSON（精确摘要输入；无末尾换行）：

```text
{"value":333333333.3333333}
```

SHA-256（小写十六进制）：

```text
ae8992f4ba3b38d67e3b504f5c0356d51bc8c5b8394a2aeea6241623003c0635
```

#### A.4.15 请求逻辑对象，Number 参数

Canonical JSON（精确摘要输入；无末尾换行）：

```text
{"command":"example_action","params":{"value":1.5}}
```

SHA-256（小写十六进制）：

```text
b0dd17a524b8711925f45327f2eaa1d878a91a46cf54f487b24ded37dac2fe24
```

该 Manifest 示例的 `manifestId` 为 `sha256:c59a64516ec2b9a7baa3695d65783d375000b6d323c4446e7df9abe6e144bda3`。`value` 与 `example_action` 为摘要校验用的示例字段和命令；Number 按相应 Profile 字段的范围与分辨率校验。

一致性校验还须满足：

**表 27：Number 请求摘要一致性检查**

| 输入变化 | 必须得到的结果 |
|---|---|
| 对象 Key 顺序或 Token 间空白改变，解码后的值相同 | Canonical Bytes 和摘要相同 |
| Number 输入 `1.5`、`1.500` 或 `15e-1` | 规范化为 `1.5`，Canonical Bytes 和摘要相同；`1`、`1.0`、`1e0` 同理规范化为 `1` |
| Number 输入 `0.000001` / `1e-6`，或 `0.0000001` / `1e-7` | 前一组规范化为 `0.000001`，后一组为 `1e-7`；同组摘要相同 |
| Number 输入 `333333333.33333329` | 按 binary64 和最短有效数字规则规范化为 `333333333.3333333`，不是按 Profile 显示精度四舍五入 |
| Number 经 binary64 解释后数值不同 | 不得因显示结果相同或落在业务容差内合并；相同 requestId 仍按逻辑内容比较 |
| 字符使用直接 Unicode 或等值 JSON 转义 | 结果相同，例如 `"格"` 与 `"\u683c"` |
| 字符仅在 Unicode Normalization 后等价 | 不合并，例如 `"\u00e9"` 与 `"e\u0301"` 保持不同 |
| Manifest profiles / instances 顺序改变，集合不变 | 摘要相同；重复 Profile 名称或实例标识仍非法 |
| 普通数组顺序改变 | 规范化结果保留顺序差异，不能作为同一逻辑内容 |
| 请求的 command、params 或存在的 expiresAt 改变 | 重新计算摘要，不能仅凭相同 requestId 判为同一内容；null 与 `{}` 不等价 |
| 仅请求外层 timestamp 改变，逻辑对象不变 | Request Digest 相同；不改变第 4.3 节的重试要求 |

**非法输入样例（故意非法，保留精确原文）**

以下原始 JSON 必须在生成摘要前拒绝；不得先解析覆盖重复值或舍入非法数字后再计算：

**表 28：故意非法输入与拒绝原因**

| 原始 JSON | 拒绝原因 |
|---|---|
| `{"a":1,"a":2}` 或 `{"a":1,"\u0061":2}` | 解码后存在重复 Key |
| `{"timestamp":9007199254740992}` | 超出安全整数上界；小于 -9007199254740991 同样非法 |
| `{"timestamp":1.5}` 或 `{"timestamp":1.0}` | Integer 字段禁止小数词法；业务 Number 的小数不属于此非法项 |
| `{"timestamp":1e3}` | Integer 字段禁止指数词法，即使数值为整数也非法 |
| `{"timestamp":-0}` | 负零 |
| `{"value":-0.0}` 或 `{"value":-0e0}` | Number 同样禁止负零 |
| `{"value":1e400}` 或 `{"value":1e-400}` | 转换为 binary64 时溢出或非零下溢为零 |
| `{"value":NaN}` 或 `{"value":Infinity}` | 非法 JSON 数字；不得先转换成 null |

辅助向量须遵循上述规则及固定结果。
