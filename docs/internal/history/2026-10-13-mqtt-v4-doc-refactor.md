# MQTT V4 协议文档收敛记录（内部）

日期：2026-10-13。本文记录纯文档整理的覆盖核对及待处理问题，不是公共协议、实现升级或现场验收记录。对外只交付四份正式协议；本文不作为对外协议入口。

## 1. 范围与改动

| 文档 | 主要删减、合并和迁移 |
|---|---|
| [公共 V4](../../protocols/A-BOX设备公共MQTT协议-V4.0.md) | 50 个主章节合并为 9 个主章节和规范性附录 A；合并 Topic/QoS、请求类别/重试、Report/状态主键、完整状态/同步、心跳/Manifest/能力确认；删除重复模型树、冻结参数复述、车型举例及目标宣言；算法及向量引用保留在规范性附录 |
| [送餐车 V4](../../../../Meal_Delivery_Vehicle/docs/protocols/A-BOX送餐车业务协议-V4.0.md) | 删除公共 Topic、外层/请求/响应/Report 字段表和公共心跳示例，公共有效期、摘要、冲突及同步规则改为引用；补业务结果字段表，保留所有业务命令示例、异常码例外、业务归一化及跨重启恢复边界 |
| [巡防车 V4](../../../../Patrol_Vehicle_Chassis/docs/protocols/A-BOX巡防车底盘业务协议-V4.0.md) | 合并重复能力清单，将参数形式、类别、有效期集中到命令清单；公共有效期段落改为引用；移除内部运维入口与历史别名说明；保留租约、安全停车、回中、换向、反馈语义和全部状态字段；补完整电源请求/响应示例 |
| [清扫车 V4](../../../../Sweeper_VCU/docs/protocols/A-BOX清扫车业务协议-V4.0.md) | 删除公共 Topic 表、重复 Manifest/同步/状态恢复解释和有效期段落；保留双输出同次读取、下电优先、失败码、跨重启及状态新鲜度要求；补结果字段表及完整电源请求/响应示例；公共引用由 master 修正为本地已核实的 origin/HEAD 所指 main |

本轮未改固件、平台业务代码、测试、部署配置、Registry 或固定向量；未处理其他产品、V2/V3、内部运维协议或智驾 CAN 协议。没有提升公共或 Profile 版本，也没有更新固件构建标识或生成发布包。

原文没有需要搬迁的缓冲区、Flash 布局或线程组织正文；架构解释中的有效接口约束已并入表格，纯背景和重复说明删除。需保留的内部信息归属如下：

| 迁出项 | 内部保留位置 |
|---|---|
| 公共原第 36.4 节的只读校验器入口 | [tools/validate_mqtt_v4.py](../../../tools/validate_mqtt_v4.py)，运行说明见本文第 5 节；固定向量仍由公共附录规范性引用 |
| 巡防车 stopReason=1 的历史名称 remote_disabled | 本记录：原文仅说明历史编号；正式协议保留 `1 / reserved`、正式运行不产生及后续编号不变，不重新引入历史许可语义 |
| 巡防车原文的内部运维/标定入口 | 既有 [内部运维协议 V2.3](../../../../Patrol_Vehicle_Chassis/docs/protocols/A-BOX内部运维协议V2.3.md)，正文未修改；正式业务协议不再将它作为对外入口 |

## 2. 篇幅比较

按修改前工作区文件与修改后完整 UTF-8 文件计量，含示例、空行和 Markdown 标记；字节数不是 MQTT 包大小。

| 文档 | 原行数 | 新行数 | 原字节 | 新字节 | 字节减少 |
|---|---:|---:|---:|---:|---:|
| 公共 | 1965 | 554 | 49153 | 29291 | 40.4% |
| 送餐车 | 435 | 381 | 16700 | 14027 | 16.0% |
| 巡防车 | 174 | 193 | 10932 | 10558 | 3.4% |
| 清扫车 | 100 | 124 | 6535 | 5278 | 19.2% |

巡防车和清扫车行数因补齐可读的完整请求/响应示例而增加，正文重复规则已减少；不以强制行数目标删减契约。

## 3. 公共规范覆盖映射

旧章节号以修改前公共 V4 为准；算法从正文移入附录仍为规范性要求。

| 原章节 | 新权威位置 | 核对要点 |
|---|---|---|
| 1～2 | 1、5.3、8 | 身份、公共/业务边界、控制和上报路由、Profile 与实例 |
| 3～5 | 2.1～2.2 | 五 Topic、QoS/Retain、客户端会话、离线队列边界 |
| 6 | 3.1～3.2、附录 A | 外层、Epoch、整数词法、重复 Key、未知字段和枚举 |
| 7～12 | 4.1～4.2 | Request Schema、四种操作类别、操作唯一分类及安全停止校验 |
| 13 | 4.3 | 原始有效期、30 秒上限、到期等号、可信时间、重试结果优先及 Profile 异常码例外 |
| 14～15 | 4.2、4.4、附录 A.1～A.2 | 摘要输入、数组/字符串相等、SHA-256、冲突、分类重试、10 分钟保护和跨重启 |
| 16～18 | 4.5～4.6 | Response、省略 result、200 边界、全部公共错误码及无法关联时丢弃 |
| 19～25 | 5 | Report Schema、三类、24 小时去重、实例约束、流内顺序/回绕、异步关联 |
| 26～29 | 6 | 完整快照、自动补发、预期集合冻结/唯一、最后接收快照、Manifest 变化、提前 Report、15 秒超时及不确定状态 |
| 30～32 | 7.1、7.3 | 心跳字段、5 秒周期、15 秒离线、接收时间、动作前提及网络竞态 |
| 33～38 | 7.2～7.4、2.2、附录 A | Manifest 字段/实例变化、版本范围、摘要全字段、在线确认及重连发布顺序 |
| 39～41 | 8 | 全局命名、共享电源能力、唯一匹配、三个 Registry 及 Profile 操作/Report 登记字段 |
| 42～43 | 5.3、7.1、9 | 最新状态主键、平台校验/路由/去重/乱序边界；旧文 Broker 接收时间与实际接收时间两种表述均保留 |
| 44 | 2.3 | deviceId/ClientId、独立凭据、ACL、明文/TLS、安全与轮换 |
| 45～46 | 3.3、2.2 | 全部字段上限、禁止公共分片、自动重连精确退避及重置 |
| 47 | 9 | 兼容扩展及主版本升级条件 |
| 48～50 | 各 Schema、参数权威章节、9 | 模型树和参数重复表删除；冻结约束及禁止重拆 Topic 保留；无独立接口语义的目标宣言删除 |

## 4. 发现但未处理的问题

以下为静态文档/源码核对所得，不代表全源码符合性审计或线上行为验证。清扫车源码在开始本轮时已有未提交修改，相关观察只对应本次工作区，不推断已发布固件。所有矛盾原定义保持，不能用本记录裁定新契约。

| ID | 现有问题 | 证据与影响 | 本轮处理 |
|---|---|---|---|
| I-01 | 公共 ClientId 必须等于 deviceId；送餐车和清扫车采用其他生成规则 | 送餐车 [main.c](../../../../Meal_Delivery_Vehicle/app/Core/Src/main.c) 的 GenClientId/MQTT_ConfigInit 使用 dev_UID，MQTT_App_Init 使用配置 vid；清扫车 [cloud_service.c](../../../../Sweeper_VCU/app/Core/User/cloud/src/cloud_service.c) 的 cloud_apply_identity 及初始配置使用 dev_UID 或 sweeper_vcu_vid，公共 Topic 使用 vid。巡防车使用 vid，不一致集中在前两者 | 保留公共规则；不改 ClientId、认证或 ACL |
| I-02 | 公共数字只允许安全整数，巡防车转角 Profile/源码允许小数 | 巡防车协议 steeringDeg 精度 0.001°，多个结果/状态字段为 Number；[chassis_cloud_protocol.c](../../../../Patrol_Vehicle_Chassis/app/Core/User/cloud/src/chassis_cloud_protocol.c) 接受毫度精度，[fixed_point.c](../../../../Patrol_Vehicle_Chassis/app/Core/User/common/src/fixed_point.c) 固定输出三位小数，包含整角值如 0.000。与公共数字词法冲突 | 不改类型、单位、编码或公共整数规则 |
| I-03 | 巡防车部分边界比公共上限小一位/一字节 | 公共 Payload 允许 1024 字节、requestId 64 字符、deviceId 32 字符；[chassis_cloud_protocol.c](../../../../Patrol_Vehicle_Chassis/app/Core/User/cloud/src/chassis_cloud_protocol.c) 拒绝 length>1023；[chassis_cloud_protocol.h](../../../../Patrol_Vehicle_Chassis/app/Core/User/cloud/include/chassis_cloud_protocol.h) 的 request_id[64] 配合复制条件最多接受 63 字符；[mqtt_core_adapter.c](../../../../Patrol_Vehicle_Chassis/app/Core/User/mqtt/src/mqtt_core_adapter.c) 使用 32 字节 ClientId 容量并拒绝长度>=容量 | 保留公共上限，不将实现容量写为协议限制 |
| I-04 | 巡防车运动请求一致性比较不是公共 JCS+SHA-256 | [remote_service.c](../../../../Patrol_Vehicle_Chassis/app/Core/User/cloud/src/remote_service.c) 的 request_hash 使用格式化业务元组及 32 位 FNV 风格哈希，只保留最近运动响应；公共附录固定请求摘要为 SHA-256。参数标准化比较与摘要算法、冲突覆盖范围须分开复核 | 不改算法或冲突语义 |
| I-05 | 巡防车/清扫车 power_on 的跨重启保护尚未在已读路径看到等价机制 | 巡防车 remote_service.c 初始化清零 g_power_replay；清扫车 [vehicle_power_protocol.c](../../../../Sweeper_VCU/app/Core/User/cloud/src/vehicle_power_protocol.c) ReplayInit 清零缓存，cloud_service.c 初始化调用；请求无启动会话参数，已读路径未见该记录持久化。公共/清扫车要求跨重启防重，需复核完整调用链和现场重放 | 保留既有保护要求；不把 RAM 缓存现状改成契约 |
| I-06 | 送餐车格口查询非法有效期的错误码未明确对齐 | 协议只明定 open_locker 非法有效期使用4005，其余非法参数引用公共4001；[mqtt_app_cmd.c](../../../../Meal_Delivery_Vehicle/app/Core/User/cloud/src/mqtt_app_cmd.c) 对 open_locker/check_locker_status 共用有效期非法分支，返回4005 | 保留原异常码例外范围，记录待澄清 |
| I-07 | sync_state 的 CORE 与 QUERY 标记并存 | 公共业务类别为 QUERY，但 [v4/registry.json](../../protocols/v4/registry.json) classification 为 CORE，送餐车清单也写 CORE，清扫车写 QUERY；可能分别表示归属和执行类别，旧字段未区分两个概念 | 保留送餐车 CORE；不改 Registry 或运行分类 |
| I-08 | TLS 证书要求与已存在 PSK 模式缺少规范边界 | 公共协议对使用 TLS 一律要求证书链/主机名/有效时间/SNI；[abox_ec800_tls.c](../../../components/ec800/src/abox_ec800_tls.c) Prepare 对 CA 分支才检查 CA/时间，PSK 有独立分支；既有 [内部 runtime 合同](../mqtt_runtime_contract.md) 也定义 PSK。需独立评审 PSK 是否为可替代认证模式及对应约束 | 保留冻结 TLS 要求，不在纯整理中增加例外 |

另有两项文档/执行优先级歧义：公共旧第 32 节以“实际收到心跳”计时，第 43 节指定 Broker 接收时间，分离消费链路时应明确时间来源；清扫车要求防止较旧普通 state 覆盖较新状态，但公共只精确定义有序流和 sync_state 的最后接收快照规则，不能在本轮擅自补 timestamp/sequence 策略。巡防车 remote_service.c 还在 requestId 冲突检查前执行合法 stop/power_off，需独立澄清公共冲突拒绝与 Profile 安全停止的优先关系。

## 5. 核对结论与验证边界

按第 3 节映射核对公共规范，按产品 diff 核对参数、枚举、字段、有效期和安全边界，未发现本次整理新增、删除或弱化既有接口约束。公共/产品既有矛盾未通过选择某一实现来解决。

- 公共报文 `4.0` 及 locker/device_power/text_display、vehicle_chassis、sweeper_vehicle 的 `1.0` 均保持。
- 五 Topic、QoS、Retain、会话、身份、数值/大小、摘要、错误码、有效期、保护窗口、心跳及同步超时保持。
- 送餐车保守 close、开锁失败列表、显示发送边界、取消/恢复规则及跨重启幂等保持。
- 巡防车租约、序号、目标/反馈区别、回中和停止归属、换向确认、故障禁止运动及 stopReason 编号保持。
- 清扫车同次读取、下电优先、不授予运动权限、失败禁止成功、自动/周期状态及跨重启要求保持。
- 34 个 JSON 代码块可解析，含完整报文和局部 Schema 示例；完整报文按紧凑 UTF-8 序列化均未超过 1024 字节。摘要占位符仍为原文示意值，不视为有效摘要。
- 本地章节引用、算法附录锚点、向量及说明记录的文件引用已检查；远端引用仅按本地 Git 的 origin/HEAD 校正，没有验证远端网站可访问性。
- 既有只读校验命令：`python tools/validate_mqtt_v4.py ../Sweeper_VCU/docs/protocols/v4/profile-registry.json`，在 ABox_Platform 执行，结果为 5 个合法/5 个非法固定向量及清扫车 Registry 通过。该工具不证明产品符合全部协议，也不证明设备或线上行为。
- 四个协议文件 git diff --check 通过；普通 diff 与 ignore-cr-at-eol 的统计一致，索引/工作区均 LF。

本轮不构建或测试固件、不部署、不提交；已有巡防车/清扫车用户改动保留，产品 platform 子模块指针不变。

最后范围复核发现清扫车另有工作继续更新 sweeper_control.c、vcu_rtos_test.c 及新增内部证据目录；本轮没有写入这些路径。开始快照中已记录的其余 33 个变更文件哈希未变，工作区原有变更条目仍在；22 个本地文件引用/锚点检查通过。目标协议在写回前均与开始副本逐字节一致，未覆盖并发编辑。
