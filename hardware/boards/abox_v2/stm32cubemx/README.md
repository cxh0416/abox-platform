# STM32F105 ABox 板级 CubeMX 源文件

这些启动、系统时钟、GPIO、CAN、DMA、UART 和中断源文件源自送餐车现有 STM32 配置；保留原有版权声明，只将换行统一为 LF。它们与 `board_stm32f105.c`、板载 IO Port 一起为使用该板级硬件合同的产品提供启动与外设初始化。

新产品可通过 `abox_platform_attach_f105_app` 一次接入板级源码和平台默认 HAL 配置；也可使用较低层的 `abox_platform_attach_stm32f105_board_sources`。产品提供链接脚本及 syscalls。产品不得同时编译另一份同名启动或外设实现。重新生成 CubeMX 文件后须核对这里的板型、引脚和中断处理，不能直接覆盖跨产品的公共配置。

不启用公共 CAN Port 时，STM32 初始化沿用送餐车现有 CAN 使能及中断配置；显式启用公共 Port 后，标准板级启动不再执行旧 CAN 初始化。公共 Port 不能与其他消费者同时使用同一控制器。GD V1 和 STM32 V2 的实板验证范围见平台内部硬件记录。
