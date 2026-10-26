# MQTT V4 Registry 一致性校验（内部）

内部资料，不作为业务对接协议对外交付。公共 MQTT 继续使用 version `4.0`；Registry 的 schemaVersion 是构建元数据格式版本，与报文版本、Profile 版本和固件版本分开。

## 1. 生产声明与校验链路

`abox::mqtt_registry` 将三份声明显式绑定：

1. 原 `ABoxMqttProfileDescriptor`：Profile 名称/版本、命令集合及报告 owner/type，沿用原结构和初始化方式。
2. `ABoxMqttOperationContract`：内嵌实际用于执行准入的 `ABoxMqttOperation`，以及声明式 matcher、引入版本、参数/结果 Schema、200 边界、重启策略与错误码。
3. `ABoxMqttReportContract`：报告引入版本、语义、payload Schema、完整状态标记、实例、顺序流和请求关联要求。

`ABoxMqttProfileContract` 绑定上述表及 contract/supportedMajors，`ABoxMqttRegistryContract` 表示同一固件实际启用的 Profile 集合。初始化检查 `ABoxMqttRegistry_Valid`，请求使用 `ABoxMqttRegistry_Resolve`。该函数在选择唯一操作后继续复用原 Resolve 的业务校验与有效期规则。

主机 `--describe` 使用 `ABoxMqttRegistry_Describe` 导出同一组生产表，不能在测试中另写一份期望分类或 matcher 来代替。`verify_profile_descriptor.py` 读取实际主机二进制导出，调用与 `validate_mqtt_v4.py` 相同的 `mqtt_registry.py` 规则，再逐字段比较 JSON Registry。

Schema 2 不允许用旧 TSV 结果满足完整校验。字段缺失、额外命令/报告、参数分支变化、类别/有效期/成功边界/重启策略、Schema、错误码或报告标记漂移均失败，并报告具体路径。

## 2. 参数匹配与命令类别

新操作表的 matcher 为 const 数据，运行时匹配与校验导出共用这份数据。支持：

- `{ "field": "data.params.action", "equals": "extend" }`；equals 可为 String、Integer、Number、Boolean 或显式 null。
- `allOf` / `anyOf` 非空组合；null matcher 表示整条命令。
- field 为 `data.command`、`data.params` 或其下的点分对象键；不使用请求 ID、时间或带副作用 callback 决定类别。

路径最长 128 字节，每个业务键最长 64 字节；matcher 最多 8 层、64 个节点。离线重叠分析最多展开 256 个条件分支，超出时明确失败。这些是 Registry 声明器的能力边界，不是新增公共 MQTT 字段或业务参数限制。

String、Boolean、null 按类型匹配，缺失字段不同于 null；Number 按数值匹配，true 不等于 1。allOf/anyOf 子条件重排不改变逻辑。离线校验拒绝不可达分支和重叠分支，编译集合还检查同 command 在多个启用 Profile 之间的路由冲突；运行时零匹配或多匹配返回 4001，不靠表顺序消歧。

运行时 `classification` 仍只有 QUERY、DISCRETE_ACTION、CONTINUOUS_SESSION、SAFETY_STOP。整条命令的所有分支类别相同，旧 C 命令描述符必须登记该类别；类别混合时登记 `ABOX_MQTT_COMMAND_PARAMETERIZED`。这个值仅为 C/Registry 聚合标记，不是第五种可执行类别，也不进入 MQTT 报文。完整校验会核对该投影，不能继续把混合 stop 的命令笼统声明为离散动作。

完整声明的 `runtime.matches` 必须为 NULL，以免 callback 与可见 matcher 漂移。原 `runtime.validate` 保留给无副作用的业务参数检查；旧 callback 操作表可继续走旧 Resolve，但不能冒充完整 Registry 校验。Core sync_state 保留在公共 QUERY 路径，Profile 不得覆盖。

## 3. Schema 2 元数据

顶层保留 `protocolVersion: "4.0"`，支持单个 `profile` 或完整 `profiles` 数组。Profile 保留原 name/currentVersion/supportedMajors/contract/commands/reports 字段。

命令层保留 name/introducedVersion/semanticDescription/applicableProfiles/classification，增加 `operations`。每个操作显式声明：

| 字段 | 来源与约束 |
|---|---|
| operationMatcher | 生产 matcher；无条件操作显式 null |
| classification | 实际运行时类别，不能为 CORE/PARAMETERIZED |
| expiresAt | required / optional / forbidden，对应运行时 expiry |
| maxValidity | uint32 毫秒；required 必须大于 0，forbidden 必须为 0；离散动作不超过 30000 |
| params / result | JSON Schema 对象或 Boolean；无 result 时为 null |
| 200Boundary | 产品对该操作定义的成功边界文本 |
| rebootPolicy | 产品明确的跨重启策略，保留原 Profile 安全合同 |
| errorCodes | 去重的 uint16 错误码集合，不含成功码 200 |

报告除原名称、owner、类型、版本、描述和 reportName matcher 外，登记 payload、fullState、instanceId(required/forbidden)、stream(strict/none)、requestCorrelation。fullState 只能用于 state 报告。

完整声明核对 supportedMajors、Profile 版本、引入版本、全局命令语义与报告唯一性。Schema 2 的未知元数据字段明确拒绝，避免新增声明被旧校验器静默忽略；这是构建元数据检查，不改变公共报文对安全可忽略扩展字段的兼容规则。

Schema 中的小数约束允许使用公共 Number 编码；timestamp/expiresAt 等公共 Integer 的词法检查不受影响。Schema 对象按 JSON 值比较，普通数组保留顺序。applicableProfiles、supportedMajors、errorCodes 按集合比较，matcher 按逻辑条件比较。

完整示例是 [独立 JSON 夹具](../../tests/fixtures/v4/registry-contract.json) 与[实际 C 声明和路由测试](../../tests/abox_mqtt_registry_test.c)。二者分别维护，校验的是编译后二进制实际声明，不从期望 JSON 生成一份用于自证的测试描述符。

## 4. 主机构建入口与兼容

主机 Profile 测试链接生产声明文件与 `abox::mqtt_registry`，在 `--describe` 分支调用测试辅助头 `tests/abox_mqtt_registry_dump.h` 的 `ABoxMqttRegistry_Dump(Product_RegistryContracts())`，返回失败必须使进程非零退出。原 `tests/abox_mqtt_profile_dump.h` 和 Schema 1 TSV 导出继续保留。

统一 CMake 入口要求 BUILD_TESTING 开启且目标为主机可执行文件，默认完整校验：

```cmake
target_link_libraries(product_profile_test PRIVATE abox::mqtt_registry)
abox_platform_verify_registry(product_profile_test
    REGISTRY "${CMAKE_CURRENT_SOURCE_DIR}/profile-registry.json")
```

直接 CLI：

```powershell
python tools/verify_profile_descriptor.py tests/fixtures/v4/registry-contract.json build/host/tests/abox_mqtt_registry_test.exe --require-complete
python tools/validate_mqtt_v4.py tests/fixtures/v4/registry-contract.json
```

原两个位置参数的 CLI 不变。Schema 1 校验明确输出 `legacy names/categories/report types only`，只保证旧描述符可见内容；不能验证其未声明的参数级分类、有效期或报告 Schema。迁移旧项目时，CMake 可显式指定 LEGACY，或继续使用原 CLI；`--require-complete` 会拒绝该范围。

公共目录的 Core Registry 和旧产品 Registry 保持原 Schema 1；不会因添加校验器自动修改产品合同、操作分类、存储格式或子模块指针。每个产品后续迁移使用实际生产表补齐 Schema 2，再切换完整校验。

## 5. 验证范围

CTest 同时运行 C 路由/描述符测试、新旧二进制导出比对、字段漂移与冲突负例，以及原公共 Canonicalization/Number 向量。负例涵盖缺失/多出声明、混合类别投影错误、零/多路由、无效有效期、Schema/结果/报告标记漂移、重复 JSON Key、布尔与数值混淆、超限 matcher 和旧导出伪装完整覆盖。

该链路证明生产声明、运行时路由条件和 JSON 登记一致，不证明自定义 validate 已实现全部 JSON Schema 约束，也不能从边界文本证明硬件达到该成功状态。业务校验、安全反馈和实机行为仍由产品测试覆盖。
