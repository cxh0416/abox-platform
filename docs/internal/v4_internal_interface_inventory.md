# ABox 三产品内部接口清单与收敛方案

适用产品为送餐车 `meal_delivery_vehicle`、清扫车 `sweeper_vcu`、巡防底盘 `patrol_vehicle_chassis`。本方案按2026年10月11日本地源码的实际分发路径、条件编译和现有内部文档整理，不代表所有入口均已在现场部署或由运维平台提供操作按钮。

建议保留必要功能和现有线协议，优先收敛重复解析、响应拼装和文档口径。产品标定、格口配置继续由产品负责，实验入口保持与常规运维区分。本次仅新增此内部方案文档，不修改代码、配置或既有协议，不新增动作、Topic、请求参数或响应字段，不发送设备请求或OTA。前次提出的授时诊断扩展也不在本方案中实施。

## 清单口径

V4是公共业务协议版本。三产品内部运维文档分别标为V2.2、V2.1、V2.3，但MQTT包络均使用 `version="2.0"`；不能把文档修订号当作三个不兼容的报文版本。`internal_config` 是动作容器，不是每项能力另建一个Topic。

源码共有34个不同的MQTT命令或action名称：其中4个是巡防条件编译实验动作，默认关闭。按默认构建路径计，清扫车6个维护action加OTA、授时共8项；送餐车18个维护action加OTA、授时共20项；巡防15个常规维护action加OTA、授时共17项，打开实验开关后为21项。送餐车20项中包含3个维修调试动作，不能把默认编译可达等同于全部适合日常运维。

参数变体、日志上行、HTTPS入网和本地RS485维护在同一清单中单独列出，不计入上述34个MQTT名称。公共业务的 `sync_state`、`power_on/off`、运动与锁控命令，以及厂商CAN协议、SWD/J-Link烧录和服务端管理API不计入设备内部运维动作。

表中“有”表示本地源码已接入对应分发路径；“无”表示本次检查的该产品分发器不识别该动作；“实验”表示受编译开关控制；“条件”表示存在条件编译入口，未用已部署产物证明启用。分类表示能力归属，公共类不要求三个产品都暴露同名动作。

## 三产品接口总清单

除特别说明外，维护action通过 `internal/config` 的 `command=internal_config`、`params.action` 调用。只读描述指不请求业务执行；查询仍可能触发信息刷新、遥测观察窗口或发布回执。

| 编号 | 分类 | 现有接口 | 送餐 | 清扫 | 巡防 | 用途和收敛判断 |
|---|---|---|---|---|---|---|
| C01 | 公共 | `get_info` | 有 | 有 | 有 | 查询设备、Boot、MQTT、OTA等信息；统一已有公共字段的含义与无效值，产品字段保留 |
| C02 | 公共 | `get_log` | 有 | 有 | 有 | 读取日志；三个实现和结果形态有差异，先由现有运维适配器归一展示，不改报文 |
| C03 | 公共性质，非必选 | `clear_log` | 有 | 无 | 无 | 清空RAM日志，有副作用；保留现状，不为对齐给另外两产品新增动作 |
| C04 | 公共 | `reboot` | 有 | 有 | 有 | 受控重启；复用响应和回执能力，保留Boot、OTA及产品准入，不能仅合并命令分支 |
| C05 | 公共 | `set_mqtt` | 有 | 有 | 有 | 端点、账号、VID等持久配置迁移；已有公共trial/ConfigStore，收敛参数校验和结果口径，不新建事务 |
| C06 | 公共 | `set_mqtt_psk` | 有 | 有 | 有 | 已有PSK连接的凭据轮换；共享凭据解析，保留代次和连接准入，与首次入网区分 |
| C07 | 公共性质，按产品提供 | `start_psk_enrollment` | 有 | 有 | 无 | 触发HTTPS重新审批迁移；保留已实现产品，巡防有入网服务不等于有此MQTT动作，不补新入口 |
| C08 | 公共 | `ota_upgrade` | 有 | 有 | 有 | 独立 `internal/ota` 命令；共享Boot V2流程，保留产品artifact、Flash边界和准入；安装受理不等于最终CONFIRMED |
| C09 | 公共 | `sync_time` | 有 | 有 | 有 | 公共授时request/response，设备发起；保留现有Topic、250ms参考门槛与500ms MCU预算，不并入 `sync_state` |
| C10 | 公共诊断概念，含产品扩展 | `mqtt_runtime_get` | 无 | 无 | 有 | 查询runtime及trial，同时携带巡防remote状态；公共状态可共享内部格式化，remote保持产品负责，不扩成三个新动作 |
| P01 | 产品扩展 | `set_map` | 有 | 无 | 无 | 设置单格口双锁映射；送餐专属，可与其他映射入口复用验证与写入底层 |
| P02 | 产品扩展 | `set_map_batch` | 有 | 无 | 无 | 一包批量配置映射；与分块导入目的重叠，但大小与提交方式不同，暂保留兼容 |
| P03 | 产品扩展 | `map_batch_begin` | 有 | 无 | 无 | 开始分块映射事务；与chunk/commit/abort为同一完整流程，不是四套独立功能 |
| P04 | 产品扩展 | `map_batch_chunk` | 有 | 无 | 无 | 写入一个映射分块；复用同一端点、格口和冲突检查 |
| P05 | 产品扩展 | `map_batch_commit` | 有 | 无 | 无 | 提交完整映射事务；不以同名批量写入替代其完整性检查 |
| P06 | 产品扩展 | `map_batch_abort` | 有 | 无 | 无 | 取消分块映射事务；保留退出路径 |
| P07 | 产品扩展 | `get_map_page` | 有 | 无 | 无 | 分页读取映射；不扩展成公共平台资源查询协议 |
| P08 | 产品扩展 | `clear_map` | 有 | 无 | 无 | 清除指定映射；与覆盖写入不完全同义，保留产品入口 |
| P09 | 产品扩展 | `get_can_diag` | 无 | 无 | 有 | 读取底盘CAN、驱动器、编码器快照；与清扫CAN诊断共享展示概念，不强行共享硬件字段 |
| P10 | 产品扩展 | `get_calibration` | 无 | 无 | 有 | 读取已保存标定与兼容状态；不能替代会话查询 |
| P11 | 产品扩展 | `start_center_calibration` | 无 | 无 | 有 | 申请转向中位标定会话；保留产品安全准入与独占语义 |
| P12 | 产品扩展 | `get_center_calibration` | 无 | 无 | 有 | 带sessionId查询当前会话；源码不会延长READY空闲期限，与静态配置查询区分 |
| P13 | 产品扩展 | `jog_center_calibration` | 无 | 无 | 有 | 会实际请求受限转向点动；属于正式标定流程，不因带调试性质就删除 |
| P14 | 产品扩展 | `commit_center_calibration` | 无 | 无 | 有 | 启动标定写入和读回，先受理再观察DONE；保留异步完成语义 |
| P15 | 产品扩展 | `cancel_center_calibration` | 无 | 无 | 有 | 停止并退出标定；停稳前可仍为BUSY，不能视为普通立即取消 |
| P16 | 产品扩展诊断 | `get_center_calibration_diagnostics` | 无 | 无 | 有 | 按需读取内部阶段、失败和拒绝证据；保留与高层会话状态的边界，不让平台依赖内部phase许可控制 |
| P17 | 产品扩展 | `reset_motion_fault` | 无 | 无 | 有 | 下电且OTA空闲时复位运动故障；不恢复旧控制会话，不归为通用重启 |
| D01 | 产品维修调试 | `test_channel` | 有 | 无 | 无 | 实际驱动锁通道，不是只读；现有常规MQTT分发可达，标明用途，不直接认定可删除 |
| D02 | 产品维修调试 | `set_baud` | 有 | 无 | 无 | 改变锁板串行通信波特率；属于维修工具功能，不进入公共配置必选集 |
| D03 | 产品维修调试 | `set_feedback` | 有 | 无 | 无 | 向锁板发送反馈模式配置；保留产品工具语义，不给另外两产品补齐 |
| D04 | 实验调试 | `mqtt_runtime_candidate` | 无 | 无 | 实验 | 巡防RAM候选试连，不写Flash；与 `set_mqtt` 共用trial，但不是其别名 |
| D05 | 实验调试 | `mqtt_runtime_cancel` | 无 | 无 | 实验 | 取消实验RAM试连，有阶段约束；不与标定取消或持久配置退出混用 |
| D06 | 实验调试 | `mqtt_lab_seed_control` | 无 | 无 | 实验 | 注入实验remote状态；不属于正式生产运维合同 |
| D07 | 实验调试 | `mqtt_lab_modem_restart` | 无 | 无 | 实验 | 重启modem用于实验；保留编译隔离，不当作常规恢复入口 |
| V01 | 产品扩展，现有参数变体 | `get_info` + `canDiagnostics=true` | 无 | 有 | 无 | CAN专项子集，非新action；不能代替完整get_info的Boot/OTA核查，不新增统一选择器 |
| U01 | 公共上行 | `internal/log` 主动日志 | 有 | 有 | 有 | 设备发布告警/日志；与get_log共用日志缓存或传输，不另建诊断Topic；get_log历史重放不能计作新事件 |
| H01 | 公共入网，HTTPS | `POST /api/v1/enroll/abox/requests` | 有 | 有 | 有 | 自动申请身份/凭据，由公共入网服务和产品适配驱动；不是MQTT动作 |
| H02 | 公共入网，HTTPS | `POST /api/v1/enroll/abox/requests/{id}/poll` | 有 | 有 | 有 | 查询审批及领取结果，复用现有申请，不新增查询入口 |
| L01 | 本地维修条件入口 | RS485 JSON `action` | 条件 | 未纳入 | 未纳入 | 送餐 `InternalConfig_HandleLocalFrame`，调用点受 `LOCKCTRL_ENABLE_RS485_CONFIG` 控制；不计为第二组MQTT动作，当前部署包是否启用未验证 |

巡防D04–D07的解析受 `PATROL_CHASSIS_MQTT_LAB_TEST` 控制，CMake默认OFF。枚举中出现动作名称或处理函数残留分支，不等于默认固件可接收该动作。D01–D03没有同样的实验宏保护，不能沿用这一判断。

L01源码支持 `get_info/get_log/clear_log/set_mqtt/set_mqtt_psk/set_map/set_map_batch/map_batch_begin/map_batch_chunk/map_batch_commit/map_batch_abort/get_map_page/clear_map/test_channel/set_baud/set_feedback` 共16个action。其包络是本地JSON action与ok/msg/result响应，非MQTT V2。所查构建配置未发现宏启用，外部构建覆盖或历史包状态不在本清单结论内。

## Topic与承载保持现状

| 产品或服务 | 现有路径 | 方案 |
|---|---|---|
| 送餐、巡防运维 | `/zxwl/abox/{vid}/internal/config`、`ota`、`log`；config/ota有 `/resp` | 相同Topic模板不代表产品动作和结果完全相同；按产品适配 |
| 清扫运维 | `/zxwl/sweeper_vcu/{vid}/internal/config`、`ota`、`log`；config/ota有 `/resp` | 保留历史命名；迁移Topic会影响ACL、订阅和服务端，本次不规划改名 |
| 三产品授时 | `/zxwl/abox/{vid}/internal/time/request`、`response` | 已是公共服务，维持QoS1、非Retain及现有包络 |
| 三产品入网 | 公共HTTPS申请、poll | 入网与已有MQTT配置迁移共享试连底层，保留不同触发和恢复边界 |

## 重复与差异判断

1. **包络和响应实现可收敛，不能再次从零建公共协议层。** 公共平台已有 `ABoxCloudInternalV2_Open/PskCredentials`、`ABoxCloudResponse_Encode`；清扫和巡防已调用。送餐仍在 `mqtt_app_cmd.c` 自行解析，在 `cloud_responses.c` 用cJSON生成内部响应。后续若批准实施，优先让共同校验和编码复用现有模块，产品参数、队列和完成回调保留。响应的空result是null还是对象、product字段是否存在、整数序列化、最大长度和错误码都先回归，不能因代码合并改变报文。

2. **get_info同名但并非同一字段合同。** Boot/OTA/身份等可建立现有字段对照；送餐当前导出clockSource/clockQuality/clockErrorMs，清扫和巡防未在所查信息生成器导出。清扫CAN兼容字段与实时Bus-Off也不是同一含义。方案先标记差异与缺省规则，平台不得把缺失当0或正常；本次不追加授时字段，不扩大专项结果为常规信息。

3. **get_log不能直接统一成一种新报文。** 清扫/巡防的按需日志是分片log上行加最终lineCount/dropped；送餐当前维护处理器把AppLog_Dump字符串放在config响应result，另有主动日志传输。共同需求是读取日志，现有形态不同；先在现有运维适配器中处理，不更换Topic或强迫旧设备接受新返回格式。

4. **MQTT配置动作共享机制，仍有接口差异。** 三产品已复用公共trial、executor、maintenance和ConfigStore。清扫TLS字段允许成对缺省后沿用旧值；巡防解析要求tlsEnabled/tlsProfileId；送餐由自身调度器解释。VID变更、凭据轮换、首次入网和RAM实验试连各有前置条件，不能合成一个泛化配置动作。复用校验与生命周期，不增加第二套状态机。巡防mqtt_runtime_get中的remote对象也不应变成公共网络层字段。

5. **送餐映射多入口属于同一服务，存在收敛空间。** set_map、set_map_batch、分块事务和clear_map复用端点与冲突验证、配置提交，读分页独立。单包批量和分块批量重叠最大，但分块用于消息大小受限和完整性提交，不能仅按名称删除。先核对现有工具调用与边界测试，选已有分块流程作为后续批量工具优先路径；单条设置保留，旧批量入口兼容，暂不宣布弃用。

6. **送餐本地维护与MQTT维护重复，应共享业务服务而非共享传输包络。** 两个处理器重复映射解析、信息生成及锁板配置调用；未来可让它们调用同一产品服务。仍保留本地和MQTT响应适配，也不为未确认启用的本地入口补线上开关。未检查调用方和现场使用前，不删除源码。

7. **巡防三个标定查询有不同对象，不是纯重复。** get_calibration读保存配置；get_center_calibration读当前会话并验证sessionId；diagnostics读内部故障证据。start/jog/commit/cancel组成一个标定流程，安全结束和异步提交不能压缩成无状态写配置。可共用同一快照来源和格式化底层，维持高层状态与内部诊断边界。

8. **CAN诊断具有共同主题，硬件结果不应强行统一。** 清扫已有get_info参数变体，巡防已有get_can_diag独立action，且巡防含驱动器/编码器证据。先统一运维工具的解释和入口分组，使用现有动作，不新增get_diagnostics或通用selector。

9. **实验和维修功能先归类，不因命名就批量删除。** 巡防4个实验动作保持默认关闭和正式合同外边界；送餐3个维修动作可能用于出厂和维护，先核对工具使用。当前信息不足以判定任何一个动作已经无人使用。

10. **文档覆盖有差异，先减少重复权威来源。** 送餐V2.2主要定义Topic、大小、get_info、OTA，映射/维修动作需要结合实现和工具；巡防V2.3的生产动作表未列源码支持的set_mqtt_psk，但不能据此认定设备不支持。清扫文档与解析器均有start_psk_enrollment及set_mqtt_psk。保留产品合同为参数差异的依据，公共内部文档集中维护共同语义，产品文档引用共同部分。此方案不修改既有协议，不重编版本号。

## 收敛顺序与边界

第一步只使用这张清单校对运维工具支持范围、既有调用记录和文档，停止继续扩张接口集合。清单不自动授权删除入口或统一调用所有产品；未暴露的action不补齐。

第二步在另行批准实施后，先做线协议不变的内部复用：共同包络/响应、MQTT候选事务的重复适配、送餐映射验证和本地/MQTT业务服务。产品安全准入、Flash编解码、通讯owner及资源容量继续由产品负责。

第三步才根据工具调用和历史设备兼容性考虑已有冗余入口的弃用。set_map_batch是候选，但本方案不决定删除；实验入口隔离已有编译机制足够，不增加在线许可、开关或新的恢复层。

未来实施的验收要求：同一现有请求在重构前后Topic、字段类型、响应码、分包与异步完成语义保持兼容；覆盖重复请求、忙态、断线、超长响应和持久化失败。只运行受影响公共模块测试与产品适配回归，检查RAM/Flash/栈及最大JSON长度，不以三产品所有动作都相同作为验收标准。

当前方案不会改变正在运行的授时服务，也不将固件诊断扩展作为稳定校时的前置条件。公共V4业务协议、expiresAt与设备安全边界保持现状。

## 核对依据

- 公共包络与响应：[abox_cloud_internal_v2.c](../../components/cloud/src/abox_cloud_internal_v2.c)、[abox_cloud_response.c](../../components/cloud/src/abox_cloud_response.c)。
- 公共维护与入网：[abox_maintenance_service.c](../../components/cloud/src/abox_maintenance_service.c)、[abox_enrollment.c](../../components/enrollment/src/abox_enrollment.c)、[既有产品接入边界](new_product_onboarding.md)。
- 公共授时：[授时接口合同](../protocols/A-BOX内部授时协议-V2.0.md)、[abox_time_sync.c](../../components/core/src/abox_time_sync.c)。
- 送餐动作与返回：[maintenance_commands.c](../../../Meal_Delivery_Vehicle/app/Core/User/cloud/src/maintenance_commands.c)、[mqtt_app_cmd.c](../../../Meal_Delivery_Vehicle/app/Core/User/cloud/src/mqtt_app_cmd.c)、[cloud_responses.c](../../../Meal_Delivery_Vehicle/app/Core/User/cloud/src/cloud_responses.c)、[V2.2文档](../../../Meal_Delivery_Vehicle/docs/protocols/A-BOX内部运维协议V2.2.md)。
- 送餐本地条件入口：[internal_config.c](../../../Meal_Delivery_Vehicle/app/Core/User/system/src/internal_config.c)、[serial_bus.c](../../../Meal_Delivery_Vehicle/app/Core/User/bus/src/serial_bus.c)。
- 清扫动作与专项查询：[cloud_protocol.c](../../../Sweeper_VCU/app/Core/User/cloud/src/cloud_protocol.c)、[cloud_service.c](../../../Sweeper_VCU/app/Core/User/cloud/src/cloud_service.c)、[V2.1文档](../../../Sweeper_VCU/docs/protocols/A-BOX内部运维协议V2.1.md)。
- 巡防动作、实验边界与查询：[cloud_protocol.c](../../../Patrol_Vehicle_Chassis/app/Core/User/cloud/src/cloud_protocol.c)、[cloud_commands.c](../../../Patrol_Vehicle_Chassis/app/Core/User/cloud/src/cloud_commands.c)、[center_calibration.c](../../../Patrol_Vehicle_Chassis/app/Core/User/motion/src/center_calibration.c)、[CMake默认开关](../../../Patrol_Vehicle_Chassis/app/CMakeLists.txt)、[V2.3文档](../../../Patrol_Vehicle_Chassis/docs/protocols/A-BOX内部运维协议V2.3.md)。

本清单是内部实现与维护方案，不作为对外业务协议交付。后续源码变化应重新核对分发器和编译开关，不能只读取动作枚举或历史文档。
