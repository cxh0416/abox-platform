# 标准 STM32F105 / EC800 产品接入（内部）

公共 MQTT V4 和冻结 Boot ABI 保持原合同。产品显式声明身份、Profile、业务 handler、状态 provider、Config codec、版本和安全策略。业务协议不进入平台公共服务。

## 构建与运行入口

App CMake 使用 `abox_platform_attach_standard_app(target PRODUCT_CONFIG_DIR dir SCHEDULER BAREMETAL|FREERTOS)`。标准硬件细节留在 `targets/stm32f105_ec800_v1`，产品保留 CubeMX 启动、特殊外设及执行器。

运行时显式初始化 AT、RX ring、MQTT bridge/runtime、Enrollment、维护、Cloud 和 Boot V2 App，然后调用 `ABoxAppService_Init`。裸机主循环调用 `ABoxAppService_Poll`；FreeRTOS 由唯一通信 owner 调用。中断只投递 RX 或事件，其他任务不能直接轮询 AT。

`ABoxAppServicePort` 按产品声明的先后关系轮询 runtime/transport，托管 ready 边沿和模组恢复。`before_cloud` 可暂停连接推进；维护、Enrollment 或 OTA 占用时禁止模组重置。重置前先投递 NETWORK_CHANGED 使旧连接状态和 operation 失效，再重启电源轨。产品回调负责业务安全撤销、配置准入和网络准备。

## Cloud 与维护

`ABoxCloudService` 统一 Manifest → Heartbeat → State、响应、sync_state、事件、周期调度及精确 operation/generation 回执。`ABoxCloudServicePort` 在 Flash 中注册报文 provider、transport 和产品完成回调。响应先确认，再发送同 requestId 状态；断连和旧回执不能完成新 operation。产品动态遥测仍由产品提供。

`ABoxCloudRoutes` 注册公共 Topic 路由；产品继续使用各自 Profile descriptor 与 handler。内部 Topic/报文由产品协议适配器处理。公共内部响应 encoder 保持既有报文及响应码。队列使用调用方静态 records、stride 和 capacity；不分配 heap，送餐车双锁持久化幂等仍在产品。底盘、清扫车已替换旧 bootstrap/TX 跟踪状态，不能并存第二份缓存。可选 deferred sync storage 由调用方提供至少 65 字节。

`ABoxMaintenanceService` 组合 trial、executor、发布证明和 ConfigStore。产品提供安全准入、候选校验、证明类型、配置快照及写入回读 callback。Commit 检查 session、阶段及期限；候选保存失败只尝试一次稳定快照恢复，恢复失败保持明确失败/占用，不能报告提交成功。Config V5/V3/V2 的字节布局分别由三产品负责。

## OTA 和资源

内部包络与响应复用 `ABoxCloudInternalV2`、`ABoxCloudResponse`，产品保留Topic、动作参数和错误映射。`Open` 仍执行既有严格校验；对已有大小写不敏感、数字timestamp和有界requestId兼容路径，`Read` 从已解析root读取借用字段并返回错误阶段，不能借重构收紧旧设备报文准入。`Close` 只用于 `Open` 持有的root，`Read` 不持有root。

响应可通过 `EncodeResult` 回调直接向调用方缓冲区打印result，不需要第二份产品大小的序列化缓冲。现有 `Encode` 的NULL result仍默认为 `{}`；产品既有 `null` 或字段缺省由适配层保持，响应队列、超长回退、回执与副作用时序不由编码器接管。三产品兼容验证与范围见[内部编解码收敛记录](internal_v2_codec_convergence.md)。

`ABoxBootV2Ec800Adapter_BindServices` 使用同一 AT 和 MQTT bridge，绑定 MQTT 暂停、停止、workspace 借还及客户端。App OTA 继续使用现有 Boot V2 状态机；产品保留版本、artifact/Flash 合同、专用传输缓冲区和安全 callback。暂停、借还与网络恢复必须由通信 owner 执行。

固定故障邮箱、最小 heap/stack 和 Flash 页布局通过产品 linker 与发布合同校验。公共 callback table 使用 const Flash 存储；新服务不得复制产品 payload。`tools/service_stack_budget.json` 声明已知 owner 调用链及保留量，公共构建收集 `.su` 并在发布前检查。静态栈估计不能代替运行时高水位测量。

## 正式发布

统一入口：`tools/build_product_release.ps1 -ProductRoot <path> -AppVersion <version>`。产品必须提供 `tools/release_entry.json` 显式身份、版本参数及产品发布脚本，并提供 `tools/release_contract.json`。禁止从目录推导产品身份。

流水线只构建 App，消费冻结 `ABox_Boot.bin`，生成原有兼容清单；暂存验证冻结 Boot 哈希、向量、版本、App/State/配置边界、完整拼接及清单一致性，执行产品额外校验后整体替换。任何失败保留或恢复原 dist。底盘额外通过 `tools/artifact_store.py` 管理替换。正式状态只由 dist 清单和实际哈希确定。

## 验证与证据

公共改变只测一次；产品测 Profile、codec、证明、安全 callback 和幂等差异。三产品在整合点运行 Release 和资源检查。新修改/失败只重跑受影响部分，不重复擦配置、入网或踢 Broker。

主机、ARM、标准板与实车证据分开记录。标准板没有整板电源控制，不能声称冷上电/pre-App 问题已验收；Broker kick 不能代替完整蜂窝断网，ready 状态不能代替持续 OTA 下载与恢复。COM3 为 485 业务接口。正式打包不表示现场 OTA、机械运动或反馈已验收。

此前阶段设计和记录见 [归档](history/2026-09-30-new-product-onboarding-pre-close.md)；原始证据与既有哈希保持不变。
