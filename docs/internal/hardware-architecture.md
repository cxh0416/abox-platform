# ABox 公共硬件库目录与接入方案

内部资料，不作为业务对接协议对外交付。

本文描述目标结构、迁移原则和当前实现范围；未列为已完成的目录与能力仍是目标。

## 目标结构

```text
ABox_Platform/
├── components/                 # 已有公共组件：MQTT、OTA、EC800、存储等
├── hardware/
│   ├── api/                    # ABox 自有硬件接口和行为约定
│   ├── mcu/
│   │   ├── stm32f105/          # ST HAL 适配、中断及芯片初始化
│   │   └── gd32f105/           # GD 标准固件库适配、中断及芯片初始化
│   └── boards/
│       ├── abox_v1/            # 物理板 V1 的资源、引脚和能力
│       └── abox_v2/            # 物理板 V2 的资源、引脚和能力
├── vendor/                     # 固定版本厂商源码及授权、来源、校验信息
├── targets/                    # 平台拥有的完整目标，例如公共 Boot
├── tests/                      # 主机测试和交叉编译检查
│   └── hardware/               # 独立硬件测试工程
├── cmake/                      # 选择目标、连接组件、检查组合
├── docs/                       # 接口、接入和内部验证资料
└── tools/                      # 校验与构建工具
```

## 边界

- `hardware/api` 定义 UART、GPIO、Flash、时间、临界区及后续实际需要的 CAN/DMA 能力及成功、失败、超时、缓冲区归属等语义。复用现有 `ABoxPlatformPort`、`ABoxBoardIoPort` 等接口，迁移时不平行复制一套功能相同的接口。
- `hardware/mcu` 将 ABox 接口映射到对应厂商库，管理芯片相关的启动、时钟、外设和中断；不引用送餐车双锁、显示或 MQTT 业务。
- `hardware/boards` 描述具体 PCB 的芯片、引脚、外设、供电、输入数量和极性；板卡版本与 MCU 类型分开表达。用户确认 V1 为 GD32F105RCT6，V2 为 STM32F105RCT6，两版外围相同，V2 新增四路输入；现有端子映射以板型合同和实测核验为准。V3 按最终设计增加板卡配置，不预设芯片型号。
- `vendor` 固定上游来源、版本、校验信息和授权；应用功能只接触 ABox 接口，厂商头文件限于对应硬件实现和启动部分。STM32F1 HAL 1.1.10 和 CMSIS 已固定在共享 `vendor` 目录，送餐车 STM32 App 已接入；冻结 Boot 保持既有构建输入。
- `targets` 的 Boot 构建产物包含芯片启动、内存布局、硬件适配和公共 Boot V2 逻辑；GD 目标须单独构建验证。旧 STM32 冻结目标保持原位置和既有输入输出，直到另行完成兼容迁移。
- `tests` 区分接口契约的主机测试、各实现交叉编译及实际硬件测试。硬件测试为独立的台架目标：Flash 测试只使用明确分配的测试页，不得覆盖 Boot 代码、描述符、状态、配置或产品 App；GPIO/CAN 等测试须根据接线和负载选择执行。记录实际板卡、固件、步骤和结果。
- 产品仓库选择平台支持的板卡和 MCU 组合，并提供产品业务与接线语义，例如锁控和屏幕；不能通过复制公共驱动或维护另一份公共 Boot 接入。

## 选择与兼容

构建时需显式指定并校验板卡与 MCU 组合，结合具体型号选择启动代码、链接脚本、驱动源和 Boot 产物。最终命令行和 API 以实现为准；未知或未完成的组合在配置阶段报错。不会在产品业务中散落 `#ifdef` 厂商分支，也不会仅凭型号前缀认定芯片兼容。

保留现有 `abox_platform_attach_standard_app`、`stm32f105_ec800_v1` 等接入口及默认行为；新接口先按需新增并在送餐车试点，再迁移其他产品。`stm32f105_ec800_v1` 是历史合同名，后缀不等同 PCB V1；`ABOX_BOARD_VERSION` 目前只选择现有输入映射，不代表 GD MCU 适配。

## 当前进度

- `hardware/api` 提供送餐车使用的串口、调制解调器串口和 MCU UID 接口，复用既有 `ABoxPlatformPort` 和 `ABoxBoardIoPort`。STM32 和 GD 均有试点实现；产品的锁电源与 LED 通过板载 IO Port 接入。
- `hardware/mcu/gd32f105` 提供启动、中断、FMC、UART4、USART0 DMA、TIMER1 和 UID 实现；`hardware/boards/abox_v1` 提供 GD V1 GPIO 和板级初始化。固定 GD32F10x 官方库 2.7.0；来源、授权和摘要见 `vendor/gd32f10x/2.7.0/README.md`。
- 送餐车 GD V1 App 和默认 STM32 App 均通过交叉构建；接口桩测试覆盖串口接收、DMA 边界、Flash 错误、板级 IO 和时基。V1 已完成下方所列的台架检查，GD 输入映射与外围反馈仍须核对。
- `targets/gd32f105_ec800_v1` 是 App 的硬件选择合同。`targets/gd32f105_abox_boot` 是独立 GD Boot 目标：已接入公共状态机、GD Flash 和 UFS 安装器，交叉构建及镜像布局检查通过。V1 已实测从 Flash 启动、擦写测试页以及从 UFS 稳定槽重装现有 App；候选镜像安装、试运行确认、回滚及掉电恢复尚未实测，描述符能力位保持 0，尚不可作为正式 OTA Boot。冻结 STM32 Boot 的已发布产物未修改；当前无法将 GD App 与冻结 STM32 Boot 作为正式组合打包。
- 板卡 V2 的启动、CubeMX 外设与中断源文件和输入引脚表位于 `hardware/boards/abox_v2`，兼容既有产品的 STM32 板载 IO Port 仍在 `targets/stm32f105_ec800_v1`；CAN Port 已提供 STM32/GD 实现，并有 GD V1 UART 和静默 CAN 回环台架目标；真实 CAN 总线及 STM32 实板尚未验证。其他产品继续通过既有入口接入，不因试点自动获得 GD 支持。

## 公共 Boot 安装器

`abox_boot_v2_installer` 从抽象的槽位流执行两次读取：第一次验证镜像长度范围、向量和 CRC；第二次在成功打开和解锁后只擦除 App 区域，按 4 字节写入、逐字读取校验并再次计算 CRC。首次擦除调用之前的失败返回 `PRE_ERASE_FAILED`；从首次擦除调用开始，即使擦除本身报错也返回 `INSTALL_FAILED`，由 Boot 状态机走回滚路径。Flash 页范围、缓冲区和槽号均会检查，尾部不足 4 字节以 `0xFF` 补齐。具体 UFS 协议、硬件和供电初始化由目标提供；原冻结 STM32 Boot 不使用新安装器。

## V1 台架记录（2026-11-13）

只读核对 J-Link UID `[redacted]` 和选中的 Boot 状态序号 53（`CONFIRMED`）。RAM 目标使用与正式目标相同的 Boot 代码，并通过链接替换禁止 Flash 解锁、擦除和写入；它初始化 EC800、从稳定槽 1 读取 150404 字节、验证 CRC32 `8A87DC50` 和向量，并成功打开第二次读取。Flash 解锁被 RAM 守卫拦截后，Boot 跳回原 App；CPU 未停止，CFSR/HFSR 为 0。该检查不证明 Flash 擦写或安装及回滚。

检查后读取确认：Flash Boot SHA-256 仍为 `981409ef107d5f5c56a1a80fce107c0292e2c1165668c0279ba6158f24a61c63`；状态序号仍为 53、状态 CRC 有效、App CRC 与状态一致，VTOR 为 `0x08008000`，处理器运行中。未备份或擦写设备 Flash。

冻结 STM32 Boot 的常规源码 Release 构建原有头文件搜索路径缺口：`main.c` 包含 `ota_ec_at.h`，但目标未添加 legacy OTA include 路径。现已为该目标补充头文件路径，不链接旧 OTA 功能；修复前以临时构建参数补路径的重建产物与修复后干净配置重建产物 SHA-256 同为 `575a4339f2a3a6ae6b5f5d0a89fc7cdc41d53a07b094190461a5b97411b8c771`。该重建摘要与冻结 `dist/ABox_Boot.bin` 不同，原因尚未核实，不用它替换冻结制品；冻结文件摘要未变化。

同日进行两项 Flash 执行验证。先将 GD Boot `1.0.0` 临时写入 Boot 区并回读一致，设备从 Flash 启动、初始化后跳转现有 App，CFSR/HFSR 为 0；随后恢复仓库内现有 STM32 Boot 发布文件，整段 SHA-256 与操作前一致。

为验证 GD FMC 实际操作，将 GD Boot 代码约束在 `0x08007000` 以下，单独为台架测试保留 `0x08007000..0x080077FF` 页；正式 GD Boot 和原 STM32 Boot 发布文件在该页都是全零填充。确认当前镜像与已有发布文件一致后，写入专用诊断 Boot，确认测试页全零，再执行解锁、擦除、页首和页末 4 字节编程及回读、再次擦除、全页读取确认 `0xFF`、重新上锁，均通过。随后按已有发布文件恢复 Boot 和该页填充值；App CRC 与状态页均未改变，设备重启后运行中。该测试没有覆盖 App 区或配置页，也没有测试完整镜像安装、试运行和回滚。

随后进行一次完整稳定镜像安装：现有 Boot 状态为 `CONFIRMED`、稳定槽为 1，稳定槽大小和 CRC 与当前 App 一致。专用诊断 Boot 复用公共安装器，完成 UFS 预校验后，实际擦除 App 区 109 页，写入并逐字回读 37601 个 4 字节字；安装返回成功，最终 App CRC32 为 `8A87DC50`，设备在约 44.2 秒后跳转 App，CFSR/HFSR 均为 0。该次测试实际擦写了 App，但没有修改状态页和配置页，也没有生成设备备份。最后从仓库已有发布文件恢复原 STM32 Boot，核对 Boot 摘要、App CRC、状态记录一致并复位。该验证不覆盖候选槽切换、TRIAL 确认、回滚或掉电恢复。

为保留现场诊断能力，GD Boot 使用 UART4（对应原 STM32 工程 UART5）输出启动及 Boot V2 状态事件。添加该输出后再次在 V1 从 Flash 启动，COM4 实际收到 `abox-gd-boot-1.0.0` 启动标识并成功跳转 App；之后恢复原 Boot，App CRC 与状态记录仍一致。

V1 送餐车诊断 App 以禁止 MCU Flash 写入、禁止自动播种和 OTA 请求的构建完成平台联调：设备侧 MQTT Core 约 30 秒达到 Ready，EC800 串口持续双向收发且接收错误计数为 0。该检查不单独证明业务心跳在 Broker 接收或锁控成功。首次诊断曾触发 App 自动播种，候选槽 `fw_slot0.bin` 现为 140040 字节，与诊断镜像大小一致；随后禁止自动播种，再次检查大小未变。稳定槽 1 可恢复原 App；设备状态和原 Boot 已核对恢复。具体记录见送餐车内部试点方案。

GD Boot 与 STM32 Boot 独立版本还需要服务端配合：现有 OTA 服务按产品设置统一的 `min_boot_version`，送餐车内部文档也要求 Boot 至少 2.2.0；GD `1.0.0` 即使后续具备所需功能也会被该比较拒绝。现有注册硬件合同仍沿用 STM32 名称，且产物没有完整的按 MCU 匹配与路由。启用 GD OTA 前需按真实硬件和 Boot 家族定义兼容规则，并避免向错误 MCU 下发镜像；不能简单降低共同最低版本或伪报 STM32 身份。

V1 另以公共 GD UART4 驱动和独立 RAM 目标进行 COM4 受控串口测试：主机发送 `A5 3C 96 69`，设备 RX 回调触发一次，异步 TX 完成回调触发一次，主机收到 `5A C3 69 96`，错误计数为 0，CFSR/HFSR 为 0。测试程序不写 Flash，测试结束复位回原 Boot/App。该测试验证 MCU 串口和当前 COM4 路径，不证明锁控板或屏幕动作。

GD Boot 与受保护的 GD 送餐车 App 也在 V1 上联合验证：仅台架 Boot 令状态读取返回“无状态”，使用已有的向量跳转分支，不修改实际状态页；App 关闭 Flash 修改、自动播种和 OTA 请求。COM4 同时收到 GD Boot 和 GD App 标识，设备侧 MQTT Core 约 30 秒达到 Ready，UART 接收错误为 0，CFSR/HFSR 为 0。测试后通过既有稳定槽恢复原 App，再恢复仓库已有原 Boot，CRC 和状态记录核对一致。该验证没有覆盖正常状态记录下的新 GD App 安装、TRIAL 或回滚。

所有 GD Boot 台架构建输出名为 `ABox_GD_Boot_Bench`，与常规 `ABox_GD_Boot` 区分；受保护 App 台架构建输出名为 `ABox_GD_App_Readonly_Bench`。这些构建仅用于台架，不应进入正式包。

公共经典 CAN Port 的两路 GD V1 静默回环已在 RAM 台架运行：通道 0 在 250 kbit/s、通道 1 在 500 kbit/s 下验证标准帧、扩展帧、远程帧，两路都初始化后通道 0 仍能收发，且停止顺序约束生效。静默回环不配置 CAN 引脚、不向外部总线发送帧；收发器和实际总线未验证。STM32 后端与厂商 HAL 头文件的交叉编译及主机桩测试通过，V2 实板仍待回归。接口语义和限制见 [CAN Port](can-port.md)。
