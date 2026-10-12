# ABox 三产品内部包络和响应编解码收敛

本次实现仅收敛公共包络解析和内部响应拼装。送餐车的常规内部响应与包络读取改为复用公共模块；清扫车、巡防原本已复用这两个模块，继续使用原严格解析入口。公共业务V4、内部MQTT Topic、字段、响应码、产品扩展、实验入口和授时业务保持现状，没有新增设备接口。

源码变更同步到三产品platform子模块工作区，仅涉及编解码及相关测试文件。各自原子模块指针保持不变：送餐d3e3f7a、清扫554897f、巡防2573618；未为获取新API整体升级其它平台功能。当前为待提交的代码/测试工作区变更，不是已经发布的固件版本。提交或发布时应同时记录公共模块改动及父仓子模块指针，不能只提交送餐适配层。

## 公共模块与产品适配

`ABoxCloudInternalV2_Read` 读取已解析root，不申请或释放root；借用command、params和requestId。错误阶段由调用方映射为现有响应。`ABoxCloudInternalV2_Open` 仍持有root并执行原严格准入，最后通过Close释放。

送餐采用已有语义对应的typed timestamp兼容策略，保留大小写不敏感字段查找、有界但可为空或含空白的旧requestId、数字timestamp及产品动作自行验证params的行为；不检查原先忽略的可选product字段。此描述是旧实现容错边界的记录，不是放宽发送方规范。严格策略继续要求非空可打印requestId、对象params，巡防继续检查可选product匹配；清扫仍忽略可选product。没有给严格路径新增timestamp检查。

`ABoxCloudResponse_EncodeResult` 通过回调向剩余输出空间打印result。送餐适配层使用cJSON打印result，原有空结果仍为JSON null。已有 `ABoxCloudResponse_Encode` API和行为保持不变，NULL result_json仍编码为 `{}`。超长回退沿用产品原格式，包括送餐紧凑错误响应不含result；原队列、缓存、发布确认、重启和OTA时序没有移入编码模块。公共V4响应仍走原产品生成路径，其空结果字段缺省方式保留。

响应大缓冲放在独立内部响应函数中，避免公共V4路径同时保留内部与公共两个产品大小的栈缓冲。公共编码器不增加heap或静态payload缓存，也不新增状态机。

| 兼容点 | 保持的行为 |
|---|---|
| Topic与方向 | 产品config/ota/log路径不改；授时request/response不改 |
| 送餐错误映射 | 无data/无有效ID仍无响应；版本/timestamp错误4001，command错误400；其它映射由原分发器负责 |
| 清扫与巡防错误映射 | 严格Open失败仍由原产品返回400；未知动作404、超长413及requestId恢复逻辑不改 |
| 请求大小和响应大小 | 沿用各产品既有常量，未借此次改动统一门槛 |
| 空result | 送餐内部null；清扫/巡防原默认对象；送餐公共及紧凑超长响应的字段缺省保留 |
| 结果及字符串 | 原result类型、整数UTC、requestId与msg转义、字段顺序由对照回归验证 |
| 响应队列 | 保留送餐当前一条待发送加两条排队、FIFO、队满处理及时钟格式化失败分支 |
| MQTT配置与OTA | 解析动作参数和业务回调未改，重试/异步受理/最终完成语义未改 |
| 产品扩展及实验 | 映射、标定、CAN专项、实验宏与处理器未改 |

没有发生线协议调整，因此本次不修改V2/V4规范、Topic、Schema、版本号或响应码，也不把实现记录写入对外协议。后续若批准调整字段或准入，再同步修改对应接口合同和兼容向量。

## 兼容回归

三产品对照各自修改前HEAD源码，不把三个产品的历史差异误当成相互不兼容。测试直接编译真实源码；送餐fixture执行实际命令分发器和响应队列，业务/OTA/硬件协作者为host stub，未授权控制和OTA调用会触发断言。清扫/巡防运行各自已有cloud_protocol_test，结果与基线一致。

每个产品359组公共响应向量包含40组响应码/result组合，以及319组输出容量边界，前后逐字相等。送餐还覆盖10组包络正常/错误/历史容错及ID长度输入、实际config/ota响应路径、字符串转义、对象与null结果、162个1536字节既有响应限制邻域样本、FIFO/队满、无时钟格式化、公共V4响应和错误Topic命令。1536是本次所查源码的既有内部响应常量，不是本次提高限制，也不改变其它产品阈值。

固定基线与输出摘要保存在 [兼容证据](evidence/2026-10-11-internal-v2-compatibility.json)，记录原产品及平台commit、before_after_equal和输出SHA256。重跑对照：

```powershell
python tools/test_internal_v2_products.py --baseline-manifest docs/internal/evidence/2026-10-11-internal-v2-compatibility.json
```

省略baseline-manifest时工具比较各仓HEAD与工作区，适合后续未提交改动审查；提交后要用固定基线文件复现本次原始对照。该工具不连接设备、Broker或服务器，不打包发布。若仓库不是相邻目录，显式传入meal-root、sweeper-root、patrol-root。

| 检查 | 实际结果 |
|---|---|
| 公共平台CTest | 48项全部通过，含解析、编码、队列、授时及配置事务 |
| 三产品固定基线回归 | 三产品全部通过，各359组响应向量逐字一致 |
| 送餐常规host入口 | test_meal_capabilities新增内部入口/响应fixture，6组host程序全部通过 |
| 送餐公共协议/向量 | test_public_protocol_contract及test_mqtt_profile_vectors通过 |
| 清扫现有产品回归 | run_tests -SkipPlatformTests通过，35项Python测试及现有C程序通过 |
| 巡防现有产品回归 | run_tests -SkipPlatformTests通过，40项Python测试及现有C程序通过 |
| ARM Release本地构建 | 三产品全部编译链接通过 |
| Git格式 | UTF-8、LF、diff --check；普通和ignore-cr-at-eol统计一致 |

本地ARM链接报告：送餐RAM56456B/Flash133828B，清扫RAM61432B/Flash130196B，巡防RAM61432B/Flash139640B。这些是当前各自Release缓存配置下的链接结果，包含预留内存，不是相同配置下的前后资源差值，也不是设备运行时栈高水位。未声称现场验收或未来正式包校验通过。

送餐实际ARM `.su` 静态帧为MealCloud_Respond 64B、respond_internal 1624B、queue_response_json 1584B。公共响应和内部响应走互斥分支，当前编译未把两个大缓冲同时放进主响应函数帧；这些单函数帧不等于完整调用链或运行时高水位。

本次按“先改代码和测试”的范围，仅做本地构建验证，没有更新正式dist、修改设备配置、发送设备命令或OTA，没有部署服务端或重启设备。

## 变更入口

- 公共模块：[包络实现](../../components/cloud/src/abox_cloud_internal_v2.c)、[响应实现](../../components/cloud/src/abox_cloud_response.c)。
- 送餐产品适配：[mqtt_app_cmd.c](../../../Meal_Delivery_Vehicle/app/Core/User/cloud/src/mqtt_app_cmd.c)、[cloud_responses.c](../../../Meal_Delivery_Vehicle/app/Core/User/cloud/src/cloud_responses.c)。
- 实际路径fixture：[internal_envelope_response_test.c](../../../Meal_Delivery_Vehicle/app/tests/internal_envelope_response_test.c)。
- 跨产品对照入口：[test_internal_v2_products.py](../../tools/test_internal_v2_products.py)。

原[接口清单](v4_internal_interface_inventory.md)仍作为方案范围依据，本次没有实施映射、多入口诊断或实验入口的后续收敛。
