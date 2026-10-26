# MQTT V4 公共组件接入（内部）

内部资料，不作为业务对接协议对外交付。对外字段、大小、类别和时序以[公共 V4 协议](../protocols/A-BOX设备公共MQTT协议-V4.0.md)为准。

## 1. 组件与边界

| CMake 目标 | 公共职责 | 产品提供 |
|---|---|---|
| `abox::mqtt_request` | 严格请求信封、ID/名称/版本、长度、摘要、按参数唯一分类、有效期校验 | Number 字段声明、业务参数校验与明确允许的集合归一化 |
| `abox::mqtt_registry` | 生产操作表、C 描述符和 JSON Registry 的完整一致性校验；声明式参数匹配 | 操作/报告合同、Schema、成功边界与业务校验 callback |
| `abox::mqtt_idempotency` | 离散动作预备提交、一次派发、首次完整响应保存/重放、冲突、保护期和中断恢复 | 调用方记录/响应内存、持久提交与回读接口 |
| `abox::ec800_ufs_at` | CMEE、文件打开、分块 RAW 读写、关闭、缺失证明、超时隔离 | 同一 AT 实例、独占 owner；需要时提供资源租用接口 |
| `abox::ufs_journal` | 固定大小 A/B 文件、版本/长度/序号/CRC、回读校验、首次使用及损坏恢复 | 快照 codec、独立初始化标记、文件路径和借用 workspace |
| `abox::mqtt_idem_ufs` | 幂等处理与 UFS 日志的完整组合、加载门禁及重置失效 | 设备命名空间、资源借还、初始化标记 |
| `abox::mqtt_backoff` | 首次延迟、失败退避、设备差异抖动和稳定在线重置 | 设备与启动差异 seed |
| `abox::cloud_v4` | Manifest/Heartbeat/Response/Report、同步集合、报告/流 ID、不可变重传报文、内部授时客户端 | Profile 表、完整状态键、业务 payload、唯一启动源与认证连接状态 |

现有 `CloudService/Bootstrap`、MQTT Bridge/Runtime/TLS、精确 operation/generation 回执、Queue、Clock/TimeSync 和 ConfigStore 继续使用。不增加第二套通信 owner、控制任务或 MQTT 会话。完整 Registry 将生产操作表、旧命令级 C 描述符和 JSON 登记显式绑定，保留既有描述符初始化和磁盘 ABI；接入与旧校验范围见 [Registry 校验说明](mqtt_v4_registry_validation.md)。

组件使用调用方存储，没有私有大快照或产品全局变量。请求解析继续使用公共 cJSON；调用方必须检查分配/编码失败并关闭请求树。业务 CAN、GPIO、升降/电源状态机、安全停止、租约授权及控制源仲裁保留在产品。

## 2. 构建和运行顺序

已有标准 App 可按需链接，不强制所有产品增加 UFS 或动态能力：

```cmake
target_link_libraries(Product_App
    abox::cloud_v4 abox::mqtt_idem_ufs abox::ec800_ufs_at)
```

所有 Poll、事件和回执由同一个通信 owner 串行调用。先初始化 AT、MQTT Bridge/Runtime 和 OTA workspace，再注册文件 backend 的独立 owner，初始化日志与幂等组合。初始化返回值必须检查。资源不足时只拒绝/推迟依赖该资源的新动作，不把 UFS 就绪作为心跳、查询或安全停止的前置条件。

`ABoxEc800UfsAt_Init` 的 owner 必须是未占用的产品 owner。可以使用 OTA 的既有可借用 buffer，但不能直接抢占 OTA owner/RAW handler。外层 Journal 的 acquire/release 覆盖整个事务；如果它已提供资源互斥，不再给文件 backend 叠加同一份非重入租用。

## 3. 请求与离散动作

1. `ABoxMqttRequest_Decode` 校验公共外层并保留可信 requestId；解析失败或 ID 不可信时丢弃。所有路径调用 `Close`。
2. 完整 Registry 路径使用 `ABoxMqttRegistry_Resolve`，从生产声明式 matcher 选出唯一操作，再复用原 Resolve 的无副作用业务 validate 和有效期检查。旧 `ABoxMqttRequest_Resolve` 保留 callback 适配。`sync_state` 为公共 QUERY，不接受 Profile 重释。离散动作必须声明不大于 30 秒的 maxValidity。
3. 有集合归一化需求时，在业务校验后调用 `Normalize`，只处理 Profile 明定的集合；普通数组不得排序。
4. 先 `ABoxMqttIdem_Find`。冲突返回 4006；已完成记录重放 `Response` 返回的原始完整报文；预备/执行中记录不再次派发；重启恢复的未完成记录返回不确定结果。重复请求的查找不依赖当前 UTC。
5. 仅新请求执行 `CheckNew/Begin`。`Begin` 预留记录，Poll 持久化并回读确认后，`TakeExecutable` 再次检查可信时间与有效期，并且只授予一次派发。
6. 产品执行原业务逻辑。达到该 Profile 的响应边界后，把完整响应交给 `Complete`；最终持久化确认前 `Response` 不可取出。200 仍不统一表示机械完成。

QUERY 与 SAFETY_STOP 不进入持久离散动作队列。安全停止能否重复执行、是否绕过普通 Busy、何时确认停止，继续由对应 Profile 和产品定义；公共层不代发 CAN 或改写安全顺序。

`ABoxMqttIdemEntry` 和响应区由调用方分配；每条响应容量可按产品最大实际响应选择，最多 1025 字节（含 NUL），不可在执行后才发现首次响应无法保存。容量满且记录未过 `expiresAt + 10 分钟` 时返回 Busy，不驱逐保护中的记录。回收同时检查可信 UTC 下界及同启动单调保护截止时间。

## 4. UFS 日志与恢复

`ABoxMqttIdemUfs` 直接组合记录 codec、Journal 和文件接口。`Load` 读取两个文件，识别有效的新代次并恢复完整记录；两份均检查且修复完成前，公共 Find/派发接口保持不可用。

文件格式为公共 `A4J1` 外层和 `M4I1` 幂等快照，不兼容清扫车 `PWR1`、送餐车 `IDEM` 或飞翼现有 Flash 日志。产品后续迁移必须显式处理旧记录与保护窗口；不得覆盖旧文件或把旧记录损坏解释成首次使用。本轮组件提取不自动升级产品存储格式或子模块指针。

只有两个文件均被明确证明不存在且产品初始化标记为 virgin，才允许新建日志。410 open failure 必须经 exact-name QFLST 证明不存在；通用 ERROR、损坏文件、部分初始化标记不能当作新盘。已初始化但两份均缺失/损坏时禁用新离散动作。

日志每次提交把新集合写入并回读两个副本，之后才释放执行或首次响应。这样在已受理后丢失一个副本时，剩余有效副本仍含保护记录。代价是一个成功动作的 PREPARED/FINAL 共四次整快照写入和对应回读，需要产品确认 UFS 时延、写入频率及 5 秒等短有效期的余量；异步等待后仍检查有效期，超时不能执行旧意图。

文件 backend 每个 RAW 块不超过公共 AT RAW 上限，可以处理大于 1024 字节的内部日志；这不放宽公共 MQTT 报文上限。Journal 预留一个读探测字节，只接受已关闭句柄且 EOF 恰好位于预期文件长度的快照；有效 CRC 前缀后多出字节也按损坏处理。短文件加载可尝试另一副本，VERIFY 长度不符仍失败。超时、迟到 RAW 或不确定句柄关闭隔离 AT，交给既有物理模组恢复路径，禁止直接清隔离标记。

产品提供的持久首次使用标记、路径和设备 ID 构成一个命名空间。改号时使用独立命名空间或显式迁移，不在仍有保护记录时隐式清空。序号耗尽返回故障，不允许回绕选择旧日志。

组合 Init 检查 workspace 最坏容量；调用方的 entries、responses 与 workspace 必须互不重叠。workspace 可来自现有 OTA 借用区，无需新增长期大缓冲。资源租用拒绝为可重试 Busy；事务失败在本次 MCU 启动内保持禁用，模组 RDY/重连不会清除该状态，避免反复读取同一坏文件影响通信。模组物理重置并排空后调用 `OnModemReset`；未失败的日志重新 Load 完成后才接受新动作。维修后的显式重新初始化由产品受控恢复入口处理，仍需读取和保留既有保护记录。

## 5. Manifest、同步与 Report

`ABoxCloudV4Capabilities_Set` 登记实际启用的 Profile 集合。传入“同 firmware 唯一确定能力”为假时，统一生成 manifestId 并由同一对象构造心跳。配置字符串及集合在下次 Set 前保持不可变；generation 改变时调用现有 Cloud bootstrap 的 NetworkChanged/SetReady 重新声明能力。固定能力可按协议省略摘要，不新增业务字段。

完整状态登记为 `(report_name, instance_id)` 表。每个保留的 sync requestId 使用一个 `ABoxCloudV4Sync` 计划，首次 Begin 冻结键集合；同 ID 同摘要重试不采用后来传入的新集合，冲突返回 4006。`ABoxCloudV4Sync_Queue` 将冻结数量交给现有 CloudService 调度；provider 按该计划的 key 和 job.state_index 编码，运行时状态数量变化不截断已接受的轮次。另一个请求使用另一个计划，容量不足返回 Busy。键字符串保持不可变；只有所属请求缓存被明确释放时才 Clear。重试生成新业务快照及新 reportId，沿用原关联 ID 和键集合。

`ReportRegistered` 核对启用 Profile、报告名/类型和实例集合；业务 payload 的范围和完整性仍由状态 provider 保证。公共编码器检查整包及 result/payload 上限。Response 超限生成完整 500；Report 编码失败不能发送半份 JSON。

`ABoxCloudV4Ids` 的 nonce 必须由目标提供跨启动唯一来源，例如既有可靠随机源或持久启动代次。仅 UID、未同步 UTC 或相同启动时刻不足以满足该合同；公共层不假装替目标创造熵。计数回绕时换 nonce，取不到新值则停止分配。严格有序流回绕前切换 streamId。

`ABoxCloudV4Packet` 直接使用调用方报文存储。失败、断连、旧代次回执不清除报文；重传沿用同 ID、timestamp 和 payload。只有明确排入新逻辑快照时才生成新 ID。它与现有 Cloud operation/generation 调度配合，不能为同一报文再建立第二份产品缓存。

## 6. 重连与内部授时

V4 连接在 Bridge 初始化后、首次 Start 之前调用：

```c
ABoxMqttEc800_UseV4Backoff(&bridge.transport, device_boot_seed);
```

公共 transport 实际使用首次 0～4 秒、失败上限 4/8/16/32/60 秒、50%～100% 抖动及连续 READY 五分钟后的重置。显式维护暂停/恢复不计作连续失败。旧消费者未选择该接口时保留既有固定间隔，不改变 V3 或内部维护合同。

`ABoxTimeClient` 复用原 `ABoxTimeSync` 三次采样算法和 Clock，承接产品重复的内部 V2 授时报文。send 通过现有 MQTT 发布接口使用 QoS 1、Retain false；Poll 和 Message 都传入当前 authenticated-ready 与连接代次。认证失效、重连或改号立即丢弃旧采样；明文或旧连接响应不更新 UTC。CCLK 继续只作 bootstrap。Topic 订阅仍由现有 MQTT 配置提供。

## 7. 回归入口与范围

复用 `build/host` 的 CMake/CTest；`abox_mqtt_request_test`、`abox_cloud_v4_test`、`abox_mqtt_backoff_test`、`abox_time_client_test`、`abox_mqtt_idem_ufs_test`、`abox_ec800_ufs_at_test` 覆盖公共流程。幂等组合测试包含预备/最终快照的每个写入前缀和轮询边界重启，文件 backend 测试使用真实公共 AT 核心。

固定摘要/Number 向量继续使用 `abox_mqtt_digest_vectors`、`abox_mqtt_numbers` 和 `tools/validate_mqtt_v4.py`。公共 Boot 二进制、App/State/配置页边界不因这些 App 组件调整。

主机文件与断电模型不能证明特定 EC800 固件介质的掉电原子性、真实擦写寿命、联网授时或机械运动。产品接入后仍验证原安全回调、资源预算、UFS 模组重置与实机掉电路径。
