# A-Box 公共平台合同

本文描述 `ABox_Platform v0.3.0` 的当前集成边界。

## 1. 平台边界

平台由两层组成：

1. 与调度器无关的公共接口和可复用手写组件；
2. 面向 `abox_stm32f105_ec800_v1` 硬件合同的完整公共 Boot 目标 `targets/stm32f105_abox_boot`。

公共组件包括：

- `abox::core`：产品配置校验和硬件回调端口；
- `abox::board_io`：可选板载 PVIN、继电器、输入消抖和 LED 控制；STM32F105 映射通过硬件合同接入，产品负责输出授权及负载语义；
- `abox::cjson`：统一 JSON 库；
- `abox::ec_power`：EC800 电源时序；
- `abox::ec800`：HAL/调度器无关的 AT 命令队列、动态 owner、RAW 接收和 USART 循环 DMA drain；
- `abox::https_ufs_downloader`：默认 `QHTTPGET + QHTTPREADFILE` 直写 UFS、1024 字节 Range 回退及流式完整性校验；
- `abox::ota`、`abox::boot`：尚未迁移产品使用的旧接口；
- `abox::boot_v2_common`：CRC32、向量校验、故障邮箱、Boot State V3、描述符校验和状态名称；
- `abox::boot_v2_app`：组合公共下载器，负责 CA/UFS 初始化、镜像校验、稳定槽初始化和 `INSTALL_PENDING` 提交；
- `abox::boot_v2_boot`：UFS 安装、中断恢复、`TRIAL`、三次失败回滚和回滚报告保持。

完整公共 Boot 目标包含 CubeMX `.ioc`、HAL/CMSIS、启动文件、链接脚本、STM32F105/EC800 硬件适配、构建脚本、校验脚本和冻结二进制。它只适用于文档声明的统一硬件与地址合同。

平台仓库拥有正式 MQTT V4 公共报文、五个公共 Topic、QoS、请求幂等、心跳、Manifest、Core 命令，以及通用 Profile 接口、描述符结构、Registry 框架和公共校验能力；MQTT V3 只作为存量迁移合同保留。Registry 区分机制和数据：Platform 提供 Registry 框架，具体 Profile 实现、静态注册数据和业务 canonicalization 由产品仓库或独立产品扩展仓提供；公共组件不枚举或命名任何具体产品 Profile。产品仓库继续拥有 App 的 CubeMX/HAL 工程、对应设备业务 Profile、CAN/RS485、执行器逻辑、产品配置页结构、App 侧 EC800 HAL/调度器端口适配、版本和发布包装。不同 MCU、Flash/RAM 容量、UART 或 EC800 电源引脚不得直接复用当前公共 Boot 二进制。

V3 到 V4 按项目整批切换。一个设备固件只运行一个公共协议版本；共用平台在跨项目迁移期间同时保留 V3/V4 Handler，并在 V4 心跳和 Manifest 均确认后才允许下发 V4 动作。协议文档和 Registry 的冻结不改变平台固件组件 API，因此不单独提升平台组件版本。

## 2. 兼容原则

- 既有 MQTT Topic、JSON 字段、CAN/RS485 帧、产品配置布局和 App 产物名不得因平台抽取而隐式改变。
- 产品 OTA/配置记录必须通过产品适配层解释；平台更新不得擦除或重新解释现有字段。
- 平台提交号属于构建元数据，不得未经协议版本升级加入既有公共设备报文。
- `ABoxBootV2Record` 是持久化 ABI：版本 3、112 字节，并保持既有状态数值；修改时必须提供明确的数据迁移。
- 256 字节 Boot 描述符固定在 `0x08007F00`，魔数为 `0x32564241`、ABI 为 1，末尾保存描述符 CRC32。
- Boot V2 必须提供 State V3、UFS A/B、App 暂存和回滚报告四项能力，即特性掩码 `0x0000000F`。描述符缺失、无效或能力不完整时，App 必须判定设备不可 OTA。
- `abox::ota` 和旧 Boot 接口只用于尚未迁移的 `Airport_vehicle`、`CageDumper`；平台升级不得让它们自动切换到 Boot V2。

## 3. 调度器与硬件端口

`abox_platform_port.h`、`abox_ec800_at.h` 和 `abox_https_ufs_downloader.h` 不依赖 FreeRTOS。裸机产品直接提供回调，FreeRTOS 产品通过任务、队列或硬件适配层提供回调；公共组件不得包含 FreeRTOS 头文件。

USART1 循环 DMA 缓冲区至少 1088 字节，裸机 drain 环至少 1152 字节，FreeRTOS/NTRIP 产品使用 4096 字节环。ISR 只调用 `ABoxEc800Rx_OnDmaEvent` 搬运数据；AT 解析在主循环或任务中执行。overflow 必须终止当前命令、重启接收链并计入 OTA 指标，禁止继续消费可能损坏的数据。

Boot V2 App 端口负责 AT 命令提交、原始数据接收、MQTT 暂停/恢复、传输缓冲区、时间和日志。Boot 端口负责 UFS 安装、Flash/向量检查、复位原因、App 跳转、延时和日志。产品适配层决定这些回调如何映射到 HAL、EC800 和调度器服务。

完整公共 Boot 已固化统一硬件适配，产品不得在自身仓库覆盖其引脚、UART 或链接布局。

## 4. Boot V2 行为

- App 默认以普通 `QHTTPGET` 获取不可变 HTTPS revision，并通过 `QHTTPREADFILE` 直写 UFS；要求 HTTP `200` 且 Content-Length 完全匹配。
- 直写命令不支持、连续两次失败或直写后的长度/CRC/向量校验失败时，App 清理会话和候选文件并自动切到一次 1024 字节 Range 下载；Range 每片必须返回 HTTP `206` 且长度准确。
- UFS 候选必须再经 `QFLST` 与 1024 字节 `QFREAD` 流式校验；最终失败不得提交安装状态，清理时先关闭已打开的候选文件，再尝试删除。关闭失败时保留原始失败结果，不将候选视为已删除。
- App 在写入 `INSTALL_PENDING` 前校验 URL、长度、CRC32 和向量表；Boot 不链接网络下载代码。
- 新烧录 App 必须先把当前不可变 revision 初始化为稳定 UFS 槽，再报告 `otaReady=true`。初始化失败不能阻塞本地业务，但必须阻止平台 OTA。
- CA/UFS 初始化对瞬时错误按 5、15、60 秒重试，终止错误每五分钟重试；探测错误为 `1001`，CA 文件错误为 `1002`。
- Boot 在破坏性操作前写入 `INSTALLING`，只从 UFS 安装，校验后进入 `TRIAL`；连续三次符合条件的试运行失败后恢复稳定槽。
- `ROLLBACK` 必须保持到平台收到真实 `get_info` 响应并由 App 确认。仅看到目标版本运行或收到 MQTT 成功响应，不能判定 OTA 成功。

## 5. Flash 与 RAM 合同

公共 Boot 的 Flash 地址为：Boot `0x08000000..0x08007FFF`、App 支持上限 `0x08008000..0x0803E7FF`、Boot State A/B `0x0803E800` 和 `0x0803F000`、产品配置页 `0x0803F800`。App 实际链接和发布范围由产品合同收紧，详见[产品接入矩阵](PRODUCT_MATRIX.md)；不得将公共上限直接作为各产品 App 容量。维修 Full 包只能包含 Boot、描述符和 App，不得覆盖 Boot State、产品配置页或产品专属保留区。

使用复位保持故障邮箱的产品，Boot 与 App 必须把 `.noinit` 固定在同一 RAM 绝对地址。链接脚本必须断言 `.bss` 在邮箱前结束，并保证 OTA 工作区之上仍保留声明的最低栈空间。

App-only 发布不得移动邮箱地址。邮箱前存在较大空闲区的产品可以把它声明为有界 newlib 堆，但 `_sbrk` 必须在链接脚本提供的明确堆上限停止，不能覆盖 `.noinit` 或 `.ota_work`。仍采用传统 BSS 后置堆布局的产品必须保留声明的最小堆、最小栈和产品运行时保护余量。

当前公共 Boot 将 RAM 分为主数据区、20 字节故障邮箱、栈余量和最低 2 KiB 栈；链接报告中的主数据区占用不能被误读为整片 64 KiB RAM 的总占用。

## 6. 发布门槛

产品日常发布只构建 App，并逐字节复用公共平台冻结的 `ABox_Boot.bin` 生成维修 Full 包。只有公共 Boot ABI、状态机或统一硬件基线发生变化时，才允许在公共平台内重新构建 Boot，并同时更新版本、描述符、冻结二进制和清单。

每次正式发布应记录产品提交、平台提交、编译器版本、App/Boot 大小、Flash 布局校验结果、CRC32 和 SHA-256。各产品继续拥有自己的产物名称和打包脚本。

构建成功只证明源码和制品一致；它不证明设备已完成 Boot 跳转、真实 UFS OTA、断电恢复、试运行确认、回滚或业务硬件验收。

## 7. 当前验证记录

2026-08-24 在 `lockctrlboard` 的本地 `temp` 测试板上，先通过 J-Link 安装包含公共下载器的 App，再连续执行三次真实 OTA。三次目标固件均为 85620 字节，均使用一次普通 `QHTTPGET` 完成整包传输并经 UFS 回读校验、Boot 安装和 `CONFIRMED` 确认；完整任务耗时分别为 87.362、92.827、92.813 秒，中位数 92.813 秒，最大值 92.827 秒。最终 `get_info` 为目标版本、`otaReady=true`、`lastOtaError=0`、`trialFailCount=0`。

该记录只证明同一块 Lock 测试板、当时 EC600E/EC800 网络和服务器条件下的正常 OTA 与性能结果，不替代 Top、Meal、Sweeper、RTK 的各自实机验收。成功指标日志目前可能在安装复位前未完成 MQTT 批量上报；物理断网、模组复位、UFS 写入掉电及 TRIAL 故障注入仍需单独留存现场证据。
