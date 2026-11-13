# RTT 调试组件使用说明

内部资料，不作为业务对接协议对外交付。

`abox::rtt` 提供 SEGGER RTT 目标端，供连接 J-Link 的调试固件使用。组件必须由产品显式链接；不会自动接管 `printf`、UART、MQTT 日志或 Boot。启用后还需要产品实际调用写入接口，才能看到日志。

## 接入

在产品已添加平台子目录后，调试配置可显式链接：

```cmake
target_link_libraries(Product_App PRIVATE abox::rtt)
```

也可通过 `abox_platform_attach_components` 的 `COMPONENTS rtt` 接入。只有在调用链接目标的构建中包含 RTT；发布配置应自行决定是否链接。SEGGER 源码来自 [SEGGERMicro/RTT](https://github.com/SEGGERMicro/RTT)，固定在提交 `9106eaa39babe53e00994d7828c1afb29390cdc1`；许可证随源码保存在 `components/rtt/third_party/LICENSE.md`。源码仅统一为 LF 换行。

例如，由产品主循环的单一写入者输出已有日志行：

```c
#include "SEGGER_RTT.h"

/* 初始化阶段，在其他 RTT 调用前执行一次。 */
SEGGER_RTT_Init();

/* line 和 length 由产品日志环读取；禁止从多个上下文并发写入。 */
unsigned written = SEGGER_RTT_Write(0, line, length);
```

产品可以沿用 `ABoxLogRing_ReadDebugLine` 的独立游标，在主循环中限量读取和发送。日志环若有中断写入，产品需按现有约定保护读取；退出临界区后再调用 RTT，不能为了调试延长业务临界区。现有日志环缓冲空间有限，消费不及时可能覆盖历史记录；其调试游标也不保证缓冲区绕回后的完整历史。不要在中断、故障处理或多个任务中并发调用 RTT；若确需并发，先针对目标核和中断优先级设计并验证互斥。不要将 RTT 输出放在安全动作的完成路径上。

默认仅使用 0 号终端，上行缓冲区 512 字节、下行 16 字节，默认 `NO_BLOCK_SKIP`：空间不足时整次写入返回 0，数据丢弃，不等待调试器；单次写入必须小于缓冲区大小。始终检查返回长度，不能把写入或调试器连接当成日志送达保证。请勿切换到阻塞模式；无探针、读取消费慢或缓冲区满时不能影响控制和 MQTT 优先级。RTT 需额外 RAM、Flash 和 CPU，产品应以实际链接结果和运行时测量确认余量。

如需调整缓冲区，给 `abox_rtt` 设置 PUBLIC 编译定义，使库和引用头文件的使用者看到相同配置，例如 `target_compile_definitions(abox_rtt PUBLIC BUFFER_SIZE_UP=1024)`。变更前确认 RAM 和日志负载；有缓存的其他 MCU 还需正确配置缓存一致性，不应直接照搬 STM32F105 配置。

## 连接与限制

使用 J-Link SWD 连接目标，在运行目标的情况下通过 RTT Viewer 或 RTT Logger 查找控制块并读取终端 0；工具应正确设置目标型号和调试接口。RTT 依赖目标内存可被探针访问，无法取代无调试器时的现场日志、无法保证复位前缓存已被读取，也不能保证死机后继续输出。早期启动、HardFault 或复位过程需要各自设计观察方式。

2026-11-11 独立测试程序已在一块 STM32F105 老 ABox 板上用 J-Link 读到实时 RTT 输出；这不代表当前产品固件已接入，也不代表其他板型、调试探针和并发场景已经验证。产品启用后应分别检查实际资源占用、无探针启动、缓冲区满时控制任务和 MQTT 的运行，以及探针连接后的日志读取。

## 可选交互控制台

`abox::rtt_console` 在上述目标端之外增加一个可选控制台，并依赖 `abox::ec800`；普通 `abox::rtt` 使用者不受影响。可显式链接该目标，或在 `abox_platform_attach_components` 中选择 `COMPONENTS ec800 rtt rtt_console`。它只适用于显式启用的本地调试固件，默认不进入任何产品。产品在同一任务或主循环上下文中执行：

1. 初始化 RTT 和产品已有的 AT 调度器，选择不与其他用户冲突的产品 owner，调用 `ABoxRttConsole_Init`；检查返回值。传入的产品和构建信息字符串须保持有效。
2. 调用 `ABoxRttConsole_Poll`，并照常运行 AT 接收和调度。不得从中断或其他任务同时调用控制台，AT 事件回调也须在同一上下文。
3. 如需日志，在同一上下文用产品已有日志环的独立调试游标取出格式化行，调用 `ABoxRttConsole_Log`；从中断共享的日志环读取时按产品规则短暂保护，离开临界区后再输出。`log off` 时不应消耗调试游标。

通过 RTT 终端发送以 CR 或 LF 结尾的命令。下行 16 字节缓冲容不下一整条命令时，上位机必须分段发送并等待固件消费，不能假设一次写入全部成功。输入最大 255 字节；超过限制的整行被拒绝。当前命令：

| 命令 | 作用 |
|---|---|
| `help`、`info` | 显示命令和产品提供的构建信息 |
| `log on`、`log off` | 开关产品接入的日志输出，默认关闭 |
| `modem info` | 查询 `AT+QGMR` |
| `ufs space`、`ufs list` | 查询 `AT+QFLDS="UFS"`、`AT+QFLST="*"` |
| `at <命令>` | 仅支持 `AT`、`AT+QGMR`、`AT+CSQ`、`AT+CEREG?` 以及上述两个 UFS 查询 |

这不是任意 AT 透传；写入、删除、二进制和需要 payload 的命令均不支持。命令通过现有调度器、普通优先级和独立 owner 提交，模组忙时返回 `ERR modem busy; retry`，不插队或中断现有事务。`query accepted` 仅表示已提交；最终结果另行显示。收到 `ERR dropped ... bytes` 或模组输入溢出提示时，响应不完整，即使最终结果是 OK 也不能把列表当作完整结果。上行队列满时丢弃数据并计数，RTT 缓冲满时不等待；调试器断开、复位或系统崩溃仍可能使丢失提示本身无法送达。仅记录命令响应的文本行，不保证捕获被其他 owner 路由的异步 URC。

默认控制台对象的输出队列为 1536 字节，输入为 256 字节，另有 RTT 本身的控制块和缓冲区；可通过一致的 `ABOX_RTT_CONSOLE_OUTPUT_SIZE` 编译定义调整，所有编译该结构的目标必须一致。队列不会随文件数量自动扩容。AT 处理器默认有 6 个槽，若产品已经占满，可在所有使用 `ABoxEc800At` 的编译单元中统一定义更大的 `ABOX_EC800_AT_HANDLER_SIZE`，并重新核对内存和 ABI；禁止覆盖已有 owner。产品必须检查实际链接结果、栈和堆余量，不得把某个台架布局直接用于其他产品。

2026-11-12 送餐车测试板通过一次临时诊断 App 验证了日志、双向命令、模组版本、UFS 容量、31 条文件列表、信号查询及不支持的删除命令拒绝。模组忙时需要重试；测试不等于任意产品已经接入或 OTA 验收通过。该台架固件位于产品临时构建目录，测试后恢复了原诊断基线；它不作为正式发布产物。[脱敏 RTT 输出及证据索引](evidence/2026-11-12-rtt-console/index.json)。
