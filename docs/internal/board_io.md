# ABox STM32F105 板载 I/O

内部资料，不作为业务对接协议对外交付。

`abox::board_io` 是不依赖 HAL、FreeRTOS 的公共通道状态组件；
`targets/stm32f105_ec800_v1/board_io_stm32f105.c` 是当前板型的 GPIO 适配。
产品通过 `abox_platform_attach_components(... HARDWARE stm32f105_ec800_v1 COMPONENTS board_io ...)`
显式链接，其他板型可用同一接口提供自己的适配。公共 Boot 不使用该组件。

硬件依据是仓内 `docs/internal/hardware/SCH_AirprotTbox_2026-06-15.pdf`，
原始来源为 2026-06-15 AirportTbox V1.0 原理图，SHA256：
`FF94C39B87A9D606FF776CB1DF4A3F963A85156DFE1A51191B3D5D0F8DB95209`。
输入通道是光耦隔离；外部通断使光耦导通后 MCU 侧读高电平。

`ABOX_BOARD_VERSION` 选择 PCB 版本：`v1` 为老版 4 路输入，`v2` 为新版
8 路输入；省略时默认 `v2`，保持既有映射。既有硬件适配目录名
`stm32f105_ec800_v1` 保留兼容，它的后缀不再用作 PCB 版本选择。

产品接线按外部端子编号描述，例如 `v1/B8`、`v2/B8`；`B8` 不代表 MCU 的 PB8。
`ABoxBoardIoStm32_ReadTerminal(ABOX_BOARD_TERMINAL_B8, &level)` 返回该板型端子的
瞬时 MCU 电平，不做消抖。端子不存在或输出指针为空时返回 0，不访问 GPIO，
也不改写输出参数。该读取不受 `ABoxBoardIo` 实例的初始化状态控制，调用前须
完成输入初始化。`ABoxBoardIoStm32_InputsInit()` 只配置对应输入，不操作电源输出和 LED。

| 外部端子 | v1 MCU | v1 IO 别名 | v2 MCU | v2 IO 别名 |
| --- | --- | --- | --- | --- |
| B4 | 无数字输入 | 无 | PC3 | IN1 |
| B5 | 无数字输入 | 无 | PC2 | IN2 |
| B6 | 无数字输入 | 无 | PC1 | IN3 |
| B7 | 无数字输入 | 无 | PC0 | IN4 |
| B8 | PA4 | IN1 | PA7 | IN5 |
| B9 | PA5 | IN2 | PA6 | IN6 |
| B10 | PA6 | IN3 | PA5 | IN7 |
| B11 | PA7 | IN4 | PA4 | IN8 |

既有 `ABoxBoardIoInput` 枚举保持按各版 IO 标号读取；v1 的 IN5..8 返回读取失败，
`Poll` 后其 `valid_mask` 位保持清除，不能当作有效的低电平。v1 输入存在掩码为
`0x0f`，v2 为 `0xff`；位号仍按 IN1..8，而非 B4..11。下面的原有通道表对应 `v2`。

构建时分别传入 `-DABOX_BOARD_VERSION=v1` 或 `-DABOX_BOARD_VERSION=v2`；
其他字符串在 CMake 配置阶段报错。板型在编译时固定，不能依靠 MCU UID 或输入
高低电平自动识别。当前选择只覆盖已核对的输入映射，其他外设差异需要独立核对。

| 通道 | MCU | 通道 | MCU |
| --- | --- | --- | --- |
| OUT1 | PA0 | IN1 | PC3 |
| OUT2 | PA1 | IN2 | PC2 |
| OUT3 | PA2 | IN3 | PC1 |
| RELAY | PC4 | IN4 | PC0 |
| LED1 | PB0 | IN5 | PA7 |
| LED2 | PB1 | IN6 | PA6 |
| LED3 | PB2 | IN7 | PA5 |
|  |  | IN8 | PA4 |

所有输出、LED 均高电平有效。初始化先写低电平，再将输出引脚配置为推挽输出；
其他产品不得由此推断它们的负载或联锁。`allowed_outputs` 位掩码只允许产品
显式开启的通道；未授权通道仍可执行关闭。输出写入会核对 MCU ODR 锁存位，
读回失败时尝试拉低，不能把这一读回解释为真实负载或继电器触点反馈。

输入每次 `ABoxBoardIo_Poll(now_ms)` 采样，稳定 20 ms 后设置有效位。
读取失败清除对应有效位，恢复后重新计时。`raw_mask` 和 `stable_mask`
只有在对应 `valid_mask` 位为 1 时才能用于业务安全判断。
LED 常亮、关闭或闪烁也由 `Poll` 推进，不在公共组件中定义业务灯义。
产品需在 10 ms 左右的既有周期调用它；不用新任务或阻塞延时。

平台主机测试和 HAL 桩只验证映射与软件行为。电源负载、触点和光耦电气表现
必须在实板上另行记录，不能用 MCU 锁存位或主机测试代替。


## 产品仅接管 LED（2026-10-09）

`ABoxBoardIo_LedsInit(io, ABoxBoardIoStm32_Port(), led_mask)` 只调用新增的
`prepare_leds` 端口，STM32 实现仅使能 GPIOB，并配置/拉低掩码选中的 PB0/PB1/PB2。
不初始化输入，不访问 PA0/PA1/PA2/PC4，不写未选中的 LED；该实例的 OutputSet/Get、
InputsGet 返回 NOT_READY。整板 `ABoxBoardIo_Init`、SafeInit、允许输出掩码和普通闪烁
行为保持原语义。端口新增可选尾字段，现有整板调用不需要提供它。

`ABoxBoardIo_LedPattern(on_ms, off_ms, pulses, pause_ms, now_ms)` 支持非对称短亮、
双闪/三闪后停顿；周期为 `(on+off)*pulses+pause`，校验总周期不超过 INT32_MAX。
同参数重复调用不重新起相位，切换灯态立即重建相位。Poll 用无符号时间差处理回绕，
迟到按相位定位，不执行无限补闪循环。业务意义仍属于产品，组件不引用 SIM/MQTT。

清扫车只接管 LED1/2（mask=3），LED3 保留；不能调用整板初始化替代产品 GPIO 初始化。
清扫车电源开关统一连接外部 B8，由端子接口按板型读取：v1 为 PA4，v2 为 PA7。
产品生成代码中保留的旧 `IO_INx` 标签不作为接线或 PCB 版本选择依据。
主机/HAL 桩验证电源输出保留、LED3 保留、GPIOA/C 不被 LED 初始化触碰，以及逐点脉冲时序。
