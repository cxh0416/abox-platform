# A-BOX 公共与清扫车业务接入归属说明（内部记录）

内部资料，不作为业务对接协议对外交付。

## 1. 目的与边界

公共协议与对应产品协议应足以供第三方实现业务接入，不额外依赖产品台账、其他产品协议、平台源码或 Registry 文件。此次补充既有设备身份、能力确认及消息关联规则的阅读路径，保留公共 V4 的 Topic、字段、版本、QoS、时序、成功边界和逐消息路由语义。

本记录不表示已发布、部署或完成第三方验收。原有工作区修改保留。

## 2. 文档改动

| 文件 | 相对本次开始时的新增 |
| --- | --- |
| `ABox_Platform/docs/protocols/A-BOX设备公共MQTT协议-V4.0.md` | 6 行：心跳及响应归属、多产品能力接入与逐设备记录规则 |
| `Sweeper_VCU/platform/docs/protocols/A-BOX设备公共MQTT协议-V4.0.md` | 同步相同的 6 行说明；不覆盖该 checkout 已有的其他差异 |
| `Sweeper_VCU/docs/protocols/A-BOX清扫车业务协议-V4.0.md` | 11 行：按 Manifest 发现清扫车能力、在线确认、报告处理、请求响应关联及双设备反例 |

业务接入以 `deviceId` 标识设备、以 Profile 声明能力；Profile 可以组合。产品协议列明自身报告，公共规则继续负责身份、心跳、请求响应及状态主键。未增加产品类型字段、握手、门禁或额外的接入接口。

## 3. 本地检查

- 三份修改文档均可按 UTF-8 解码，无 BOM、无 CR，保持 LF。相对本次开始时仅新增 6、6、11 行，原有文字未删除或替换。
- 三份文档的全部 fenced 示例逐字节保持；28 个 JSON 示例可解析，72 个表格列数一致，76 处本地或仓库链接与标题锚点检查通过。
- 公共规范 checkout 与清扫车子模块的本次新增说明相同。两份公共文档原有的目录、表题和其他内容差异未作为本次同步范围。
- 公共 checkout 与清扫车子模块分别运行 `tools/validate_mqtt_v4.py`，并传入清扫车 `profile-registry.json`，均通过：5 组有效、5 组非法公共向量，以及 7 组有效、6 组非法 Number 向量。
- 清扫车 `python -m unittest discover -s tests -p test_protocol_docs.py` 通过，3 项测试。
- 修改文档的 `git diff --check` 通过；普通与忽略 CR 的 diff 统计一致，`git ls-files --eol` 均为 `i/lf w/lf`。Git 相对 HEAD 的统计包含本次之前的未提交修改，未将其计为本次新增。

GitHub 协议链接按本地权威源码核对路径和锚点；未验证远程发布内容、访问权限或同事已收到的文件。此次没有固件代码修改、构建、版本升级、打包或部署；没有新增固定字节或行为变化，因此未运行固件功能测试。

## 4. 扩展检查范围：仅 V4

后续要求将同样的交付标准用于其他产品，并明确限定只处理 V4。本轮覆盖公共 V4、送餐车 V4、巡防车底盘 V4、清扫车 V4 及其文档入口；同步这三处产品 platform checkout 中相同的公共说明，不覆盖已有的其他子模块差异。V3、顶飞翼、旧 R-BOX 及其他接口均未修改。

### 4.1 核对的实现证据

- 送餐车 `app/Core/User/cloud/src/meal_mqtt_profile.c` 声明 `locker 1.0`、`device_power 1.0`、`text_display 1.0`；`mqtt_app_hb.c` 的 `v4_publish_manifest()` 构造三项能力并以 QoS 1、Retain 1 发布。
- 巡防底盘 `app/Core/User/cloud/src/patrol_mqtt_profile.c` 声明 `vehicle_chassis 1.0`；`cloud_messages.c` 的 `CloudMessages_BuildManifest()` 编码该能力，`cloud_tx.c` 的 Manifest job 以 Retain 1 发布。
- 清扫车 `app/Core/User/cloud/src/sweeper_mqtt_profile.c` 声明 `sweeper_vehicle 1.1`；`cloud_service.c` 的 Manifest job 使用 `CloudMessages_BuildManifest()`，Retain 1。
- 公共 `components/cloud/src/abox_cloud_service.c` 在连接就绪后先调度 Manifest，再调度 Heartbeat、完整状态。没有公共查询 Manifest 命令；`sync_state` 的响应集合用于完整业务状态。
- 巡防底盘可选诊断的既有线格式从 `shared/include/motion_fault.h`、`patrol_runtime.c` 及 Git HEAD 的 `cloud_messages.c` 编码规则核对：版本 1 的 29 个元素、索引 5～9 的有符号解释，以及 11 个通信诊断字段。当前未提交编码器省略这些可选诊断；文档保持可省略及不作为控制条件的边界，不删除历史兼容字段，也不要求重新发送它们。

以上为源码证据，不表示设备或 Broker 实收验收。

### 4.2 补齐的合同说明

- 公共第 7.3 节明确设备主动发布 Manifest、平台订阅 Retained 获取、缺失或乱序到达时的处理，以及 `sync_state` 不查询 Manifest。
- 公共新增第 7.5 节，给出订阅、设备识别、能力确认、状态恢复、请求关联及变化处理的完整流程，沿用原有动作下发条件。
- 送餐车第 2.2 节展开三 Profile 的业务对应关系，明确通用电源能力不能证明设备具有完整格口与显示业务；巡防底盘第 3.2 节明确 `vehicle_chassis`、状态及控制会话归属；清扫车第 2.2 节引用同一公共流程。
- 巡防底盘第 5.2 节展开原先只有 Array/Object 的可选诊断线格式，接入方无需查看平台源码才能理解表示方式；内部控制算法和厂商码解释不进入业务接口依赖。
- 三个产品入口明确对外业务开发只需公共 V4 与对应产品协议，内部运维分列。没有新增产品字段、Topic、握手或门禁，没有更改报告路由、消息时序及命令成功语义。

### 4.3 检查结果

- 四份主要协议及三处子模块公共文档的 72 个 JSON 示例可解析，无重复 Key；完整报文示例与业务子对象分别核对 1024、768 字节限制。全部原有 fenced 示例相对本轮开始时逐字节保持。
- 162 个表格列数一致，188 处协议链接和锚点核对通过；四份主要协议的二、三级目录覆盖完整。各子模块中的本次公共说明与权威 checkout 完全相同，不要求原有整份文档字节相同。
- 公共与各产品的 V4 registry/vector 校验通过。权威 checkout 与清扫车 checkout 同时覆盖 Number 向量；送餐车、巡防底盘 checkout 的现有脚本覆盖各自已有的 5 组有效、5 组非法公共向量。
- 送餐车既有 `tools/test_public_protocol_contract.py` 原先以单行 JSON 文本匹配 Profile；本轮开始时的多行示例同样无法通过该检查。已改为解析示例 JSON 后核对三项 Profile，保留其他公共合同断言，15 个 JSON 示例检查通过。未为通过检查而改写协议示例。
- 清扫车 `test_protocol_docs.py` 的 3 项既有检查通过。
- 相对本轮开始时，主要公共协议新增 16 行；送餐车业务协议新增 16 行；巡防底盘业务协议 +68/-3 行；清扫车业务协议 +7/-2 行。额外修改仅为相应入口、三处公共文档说明及一处既有文档检查器。原有未提交业务代码、制品及历史资料保留。

本轮变更均为 UTF-8、LF；普通与忽略 CR 的 diff 统计、`git diff --check` 和 `git ls-files --eol` 另行核对。没有提交、发布或部署，外部链接仍仅验证本地权威路径与锚点。
