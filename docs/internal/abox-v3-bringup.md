# ABox V3.3 接入与验收

内部资料，不作为业务对接协议对外交付。

## 板型与产品接入

F105 App 使用 `HARDWARE gd32f105_ec800_v3` 和 `ABOX_BOARD_VERSION=v3`。GD32F105RCT6 使用 8 MHz 晶振。调用 `ABoxBoard_Init` 后再初始化 `ABoxBoardIo`、CAN 和产品逻辑。V3 输出有效位是 `ABOX_BOARD_IO_V3_OUTPUTS`。产品将实际允许驱动的通道传给 `ABoxBoardIo_Init`。

| 功能 | V3.3 资源和接口 |
|---|---|
| 端子 B4–B11 | PC3/2/1/0、PB7/6/5/4，按顺序对应 IN1–IN8 |
| PVIN 直通、12 V 和继电器 | PA0/PA1 对应 `PWR_OUT1/2`，PA2/PA3 对应 `P12V_OUT1/2`，PC4 对应 `RELAY` |
| 双 CAN | CAN1 PA11/PA12、STB PB15，CAN2 PB12/PB13、STB PB14 |
| EC800E | UART1 PA9/PA10、PC7 电源、PC8 PWRKEY、PC9 DTR、PA8 RI |
| 外部串口 | RS232 用 UART3 PB10/PB11，TTL 用 UART4 PC10/PC11，RS485 用 UART5 PC12/PD2 和 PC6 方向 |
| SIM | FSA2567 的 PC5 低选实体卡、高选 eSIM。默认实体卡。EC800/SIM 电源关闭且放电后调用 `ABoxBoardV3_SelectSim` |
| 指示与调试 | PB0–PB2 为可控 LED，PA15 为蜂鸣器，PB3 为 SWO。SWD 保留，JTAG 释放 PA15 和 PB4 |
| GD25Q32E | SPI1，PA4/PA5/PA6/PA7 为 CS/SCK/MISO/MOSI，JEDEC ID 为 `C8 40 16` |

UART5 配置普通异步串口，通过 `ABoxBoardV3_Rs485Send` 发送并在 TC 后切回接收。调用方串行化 UART5 的发送和收发方向，接收仍使用公共 UART API。读到端子电平或输出锁存后，还需独立确认现场负载表现。

## NOR 分区与能力选择

| 地址 | 用途 |
|---|---|
| `0x000000–0x03FFFF` | OTA 槽 0 |
| `0x040000–0x07FFFF` | OTA 槽 1，最后一个扇区 `0x07F000–0x07FFFF` 留给本板布局的台架擦写用例 |
| `0x080000–0x080FFF` | 防重布局头，保存配置容量 |
| `0x081000–0x1DFFFF` | 防重追加日志和扇区内索引，默认 1,404 KiB |
| `0x1E0000–0x1FFFFF` | 防重擦除意图，默认 128 KiB |
| `0x200000–0x3FFFFF` | 默认预留给其他用途的 2 MiB，防重和 OTA 驱动限界均在此区域之前 |

表中按默认容量列出地址。产品通过构建参数 `-DABOX_NOR_IDEM_KIB=1536` 配置防重分区，取 12–3,584 KiB 之间的 4 KiB 整数倍。首个扇区保存布局头，擦除意图随配置调整并占 1–32 个扇区，其余用于日志。OTA 槽和防重起址固定。预留区起址和长度由 `ABOX_NOR_UNASSIGNED_OFFSET` 与 `ABOX_NOR_UNASSIGNED_SIZE` 表示。

防重首次挂载时写入布局头。已有布局与 App 的容量不匹配时，防重拒绝挂载并保留磁盘内容，恢复匹配的配置可继续读取。产品在首次部署前选定容量，带历史记录的设备调整容量需另行迁移。


需要任一 NOR 能力时，用 `abox_platform_attach_v3_nor(MyApp)` 编入板级驱动。时基可用后通过 `ABoxBoardV3_NorOpen` 初始化一个共享实例。SPI1 和 PA4–PA7 归板载 NOR 独占。产品在同一个服务任务中串行推进 NOR、幂等和 OTA。

### V4 防重

链接 `abox::mqtt_idem_nor`。以设备 ID 调用 `ABoxMqttIdemNor_Init`，随后定期执行 `ABoxMqttIdem_Poll`，等待挂载完成。挂载每轮扫描一个扇区。业务先按 V4 规则完成请求摘要和有效期检查，再用同一套 `Find`、`Begin`、`TakeExecutable`、`Complete` 和 `Response` 接口处理离散动作。

1. `Begin` 成功表示已预留预备记录和最大首次响应空间。轮询到 `ABoxMqttIdem_State` 返回 `ABOX_IDEM_PREPARED` 后，带刷新后的时钟调用 `TakeExecutable`。它仅授权一次派发。
2. `Complete` 接收不超过 1,024 字节的完整首次响应。轮询到 `ABOX_IDEM_FINAL` 后读取并复制原始响应，再交给发送队列。缓存返回的地址由存储组件借出。请求句柄只在对应事务内有效。
3. 已保存的同摘要请求按字节重放。摘要冲突沿用 4006。重启后只有预备记录的请求返回结果不确定。新动作在写满、挂载或存储异常期间沿用 Busy。NOR 擦除期间，暂时无法读取的请求可重试。查询、心跳和安全停止走产品已有路径。
4. 设备命名空间随日志保存。MQTT 重连、切 SIM 和模组重启无需重挂载。一整个扇区内的保护期届满后才能擦除。擦除意图先在独立扇区落盘，清除擦除意图前禁止复用被擦扇区。可信 UTC 和同一启动周期内的单调时间共同保护 `expiresAt + 10 分钟`。

现有 UFS 日志格式由 UFS 组件维护。产品从 UFS 防重切换到 NOR 时，应在旧动作保护窗口结束后再启用新后端。V3 同板启用 NOR OTA 时，防重分区地址和内容无需搬迁。

### NOR OTA

配置 `ABoxBootV2AppConfig.nor`，复用产品的 EC800、HTTPS、CA 和工作区绑定。配套 Boot 使用 `ABOX_GD_BOOT_NOR_AB=ON` 构建。App 用描述符检查介质能力。首次将当前 App 写入稳定槽。后续 HTTPS 下载临时文件为 `fw_nor_stage.bin`，候选写入非稳定槽，全镜像读回后检查长度、向量和 CRC，再持久化 `INSTALL_PENDING`。App 启动和确认、Boot 安装和回滚继续使用现有两页状态及 218 KiB 安装区。Boot 在 EC800 电源关闭时从 NOR 读取。

## 证据与台架记录

参考构建与主机验证命令见 [F105 接入示例](../../examples/f105_app/README.md) 和 [GD32 Boot](../../targets/gd32f105_abox_boot/README.md)。

主机用例使用软件 SPI NOR、模拟 EC800 AT 传输和模拟 MCU Flash。覆盖 V3 端子和安全输出、容量填满、重启逐条回放、损坏与断电注入、稳定槽建立、候选复制和安装中断、试运行确认及回滚。回滚后重新挂载防重日志并检查原始响应和一次性派发。交叉构建和主机测量只证明对应测试条件下的行为。

默认 1,536 KiB 配置下，主机模拟测得 1,024 字节响应 1,053 条，重启后逐条回放一致。2 字节响应可存 7,371 条。填满过程擦除次数为 0。128 个抽样未命中请求的最大 NOR 数据读取量分别为 600 字节和 18,144 字节。ARM 参考 App 的防重实例占 7,592 字节静态 RAM。板上 SPI 耗时、任务栈峰值和 UART 接收余量待到板实测。

| 台架项 | 记录内容 |
|---|---|
| 板级上电与复位 | B4–B11 输入和有效位，四路电源与继电器关断电平，CAN/STB，三种串口，RS485 最后停止位，双 SIM、蜂鸣器和 SWD |
| NOR 测试扇区 | JEDEC、擦除、跨页写、逐字节读回和恢复擦除状态 |
| 防重工况 | 1,024 条 × 1,024 字节，动作计数、各断电点重放、装满时的查找耗时和 RAM 峰值，擦除期间 UART/MQTT 收包与轮询 |
| OTA 工况 | 稳定槽、候选、确认、失败回滚，候选复制/安装断电和 EC800 断电，回滚后重放未过期防重记录 |
