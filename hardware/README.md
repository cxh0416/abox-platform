# ABox 硬件接口

公共平台将接口、STM32F105/GD32F105 实现和 ABox 板卡资源映射分开。STM32 后端使用 ST HAL 并在部分功能直接访问寄存器；GD32 后端使用 GD 标准外设库和寄存器，不依赖另一套独立的 GD HAL。产品选择板型并提供所需的初始化参数；发送入队、输出寄存器值和 Flash 控制器返回值都不能代替外设或车辆的实测结果。

## 接口

- `abox_uart.h`：物理串口 1～5 统一配置波特率、数据位、校验、停止位和流控，支持异步、同步主机、LIN、单线半双工、IrDA、智能卡、多处理器唤醒及固定长度或空闲接收、阻塞收发、DMA。同步和智能卡仅适用于物理 1～3；智能卡重试和单线方向由应用负责。板卡配置引脚、复用和中断；`ABOX_UART_ENABLED_MASK` 可裁剪实例状态。事件结构和上下文由调用方持有，Close/Open 后仍绑定；调用方串行化同一实例。
- `abox_serial_transaction.h`：可选串口请求/应答队列，使用指定 UART 实例。应用负责超时、协议解析及事务所有权。
- `abox_can_port.h`、`abox_can_events.h`：经典 CAN 配置、收发、邮箱完成和错误事件，详见 [CAN 说明](../docs/internal/can-port.md)。
- `abox_gpio.h`：物理 GPIO 输入、模拟、推挽/开漏输出及复用模式，支持按端口锁定配置直到复位。输入值和输出锁存分别读取；板卡负责确认封装、引脚复用及外部电路，配置输出前先写初值。
- `abox_pinmux.h`：配置 F105 的 UART、SPI、I2C、定时器、CAN 等复用及内部信号路由；每次显式指定 JTAG/SWD 模式，避免依赖不可可靠读回的调试配置。应用须协调板级和厂商库对 AFIO 的其他访问，选择实际封装支持的引脚并独立配置 GPIO。
- `abox_exti.h`：GPIO 0～15 外部中断线，支持上升、下降或双边沿、中断和事件模式、软件触发及共享 IRQ 分发；内部 16～18 线支持电压检测、RTC 和 USB 唤醒的 EXTI 路由。应用配置对应外设、输入和 NVIC，关闭前须与 IRQ 串行化。
- `abox_spi.h`：物理 SPI1～3、主从模式、8/16 位、极性/相位、位序和 NSS，支持全双工、双线只接收和单线双向的阻塞传输及 DMA；这些模式都可使用硬件 CRC8/CRC16，单线发送没有接收 CRC 可校验。DMA 搬运完毕后须等待 `ABoxSpi_FinishDma` 确认 CRC 和总线空闲，再释放片选。只接收主机的停钟可能受中断延迟影响，引脚和片选由调用方负责。
- `abox_usb.h`：USB OTG FS 控制器提供设备端点、SETUP、主机通道、单包收发、事件、取消及设备远程唤醒信号；应用负责描述符、枚举、重试、唤醒许可和信号时长及 USB 类协议。设备支持全速，主机支持全速和低速。控制器有 4 个双向端点、8 个主机通道、320 个 32 位 FIFO 字；收发缓冲区容量和生命周期见头文件。普通固定角色使用方式保持不变。板卡负责实际 48 MHz 时钟、引脚复用、NVIC、VBUS 检测、外部供电开关和过流保护；两个 F105 后端设备模式要求启用 VBUS 检测。使用阻塞初始化和端口复位时，平台毫秒计时必须推进；STM32 官方 HAL 的时基也必须推进。
- `abox_usb_otg.h`：提供 SRP/HNP 请求、许可、状态和事件。`ABoxUsbOtg_EnableManaged` 接收另一角色的 FIFO 配置，硬件改变角色后自动接管对应的数据后端；USB IRQ 调用 `ABoxUsb_Irq`，任务上下文及时调用 `ABoxUsb_Poll` 完成切换。原角色活动传输以取消回调结束，端点和通道失效；切换完成或失败通过事件通知。主机挂起和恢复接口提供 HNP 所需的端口操作，恢复持续时间由上层计时。上层负责 SetFeature、超时、USB 枚举和类协议，板级负责外部 VBUS；开启接口不等于协商已经成功。独立信令模式仍可由其他数据后端使用。
- `abox_i2s.h`：物理 I2S2/3 支持主从收发、Philips、左/右对齐及短/长帧 PCM，支持 16/24/32 位数据、轮询、中断和 DMA。它们分别与 SPI2/3 互斥；可配置并选用独立 I2S PLL，也可由板卡提供已配置的音频时钟及实际频率。PLL 按芯片各自的频率和倍频范围校验；引脚和 IRQ 由板卡负责。DMA 完成不代表最后一位已发送。
- `abox_i2c.h`：物理 I2C1/2 主机支持 7/10 位地址、轮询和 DMA 事务、读写及重复起始；从机支持地址匹配、收发、停止、重启和错误事件，监听时也可发起主机事务。仲裁丢失、无应答和超时分别返回；仲裁丢失时芯片对本次地址匹配的限制见头文件。外部上拉、引脚复用和总线清除由板卡负责。从机停用须与外部主机协调。
- `abox_smbus.h`：在现有 I2C 接口上执行 SMBus/PMBus 固定长度事务、动态块读取和块过程调用、Quick Command、PEC 校验、告警标志与响应地址读取及 Host Notify 接收；可选 F105 SMBus 模式以使用硬件线低超时检测。块读取要求及时处理长度字节，告警引脚及外部线路由板级配置，具体限制见头文件。
- `abox_adc.h`、`abox_dac.h`：ADC1/2 校准和单次 12 位采样，ADC1 支持多通道扫描、软件或外部触发 DMA；双 ADC 支持单次同步采样、同步扫描和同通道快速/慢速交错 DMA，双注入组支持同步及交替触发，并可与规则组 DMA 组合；同步规则组还支持同步自动注入。模拟看门狗支持规则组、注入组及中断。具体通道、采样时间、触发和 DMA 完成约束见接口说明。双通道 DAC 支持立即、软件、定时器或 EXTI9 触发，以及硬件三角波、噪声波和 DMA 输出。ADC2 无独立常规转换 DMA 请求。共享时钟、GPIO 和触发源须统一分配；寄存器值不代表实测电压。
- `abox_timer.h`、`abox_dma.h`：定时器计数、更新、TRGO、外部时钟和主从同步、输出比较、PWM、PWM 更新 DMA、寄存器序列 DMA burst、输入捕获、捕获 DMA、正交编码器和霍尔三输入异或；TIM1 支持重复计数、互补输出、死区、刹车输入及刹车事件中断。DMA burst 可选择对应定时器实际映射的更新、捕获比较、触发或换相请求；DMA 支持方向、宽度、地址递增、循环及半完成/完成/错误事件。定时器通道共用周期；PWM DMA 使用该定时器唯一的更新请求。ABox UART、DMA 和外设联动自动互斥 DMA 通道；板卡须配置 NVIC 和分发中断。
- `abox_rtc.h`：配置 LSE 晶振或外部信号、LSI、HSE/128 的 RTC 计数器、闹钟、标志、F105 校准值及信号输出；事件分发在任务上下文执行，板卡负责中断延迟处理和闹钟 EXTI。普通打开保留备份域，只有显式调用重置才会清除备份寄存器。RTC 值是原始计数，不自动代表可信 UTC。校准值只能减慢时钟，实际误差和校准效果需测量。
- `abox_crc.h`、`abox_backup.h`、`abox_pvd.h`：独立硬件 CRC 计算器、42 个 16 位备份寄存器及电压检测器。CRC 采用 F105 固定的非反射 32 位算法，字节流的组字方式由应用定义；备份寄存器与 RTC 备份域共同分配。备份域入侵检测须显式启用，检测到事件会清除全部备份数据，PC13 与 RTC 输出须协调；电压检测阈值为标称值，事件不等于精确电压测量。三者均须协调厂商库及其他所有者。
- `abox_flash.h`、`abox_flash_options.h`、`abox_mcu_clock.h`、`abox_time.h`、`abox_critical.h`：Flash 几何和分段擦写、选项字读取及完整配置、启动时钟树、时钟输出及外部时钟故障监测、平台时基和临界区。选项字写入会整体擦写，关闭读保护可能导致主 Flash 被芯片擦除；细节见头文件。启动时钟支持晶振、外部信号旁路、辅助 PLL 级联和非整数 Hz 的中间分频；STM32F105 与 GD32F105 分别按 72 MHz 和 108 MHz 上限校验。NMI 分发与故障后外设恢复由系统负责。Flash 是物理访问接口；MCU 时钟与 UTC 同步的 `abox_clock.h` 是不同接口。
- `abox_low_power.h`：Sleep、Stop、Standby 入口、唤醒标志及 Stop 后时钟恢复辅助；唤醒源、恢复前可运行的中断、系统时基和外设恢复由板级/系统所有者协调。Standby 正常唤醒通过复位返回启动流程；具体约束见头文件。
- `abox_mcu.h`、`abox_target_identity.h`：UID、复位原因快照及目标身份。清除复位原因影响全部硬件标志，应由启动阶段的单一所有者执行。
- `abox_watchdog_port.h`、`abox_wwdg.h`：独立看门狗和窗口看门狗分别配置与刷新；窗口看门狗支持提前中断和调试暂停，启动后只能通过复位停止。独立看门狗的实际超时取决于低速振荡器误差，窗口看门狗取决于 APB1 时钟；调用方负责刷新时机和中断配置。
- `ABoxPlatformPort` 为既有公共服务提供 Flash、单调时间及其他板级回调；标准板实现将 Flash 调用转入公共 Flash 接口。`ABoxBoardIoPort` 描述现有板载端子与输出语义。

F105 串口支持 8/9 位无校验、7/8 位加奇偶校验，停止位和流控按物理实例及模式校验；物理 1～3 可选择 RTS/CTS、0.5/1.5 停止位，板卡需自行配置引脚。物理 5 没有 DMA 请求；7 位加校验的 DMA 接收不支持，避免把校验位当作数据。DMA 循环接收容量须为不小于 2 的偶数。实际波特率按硬件时钟分频，应用须核对允许误差。

当前 STM32F105RC 和 GD32F105RC 没有片上以太网 MAC；将来使用其他 MCU 或外置网卡时需另加对应后端。芯片范围见 [ST 参考手册](https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)和 [GD32F105 数据手册](https://www.gd32mcu.com/data/documents/datasheet/GD32F105xx%20Datasheet_Rev2.3.pdf)。

## 产品接入

符合现有 F105/EC800 板卡合同的产品可调用：

```cmake
abox_platform_attach_f105_app(MyApp
    HARDWARE "${ABOX_HARDWARE}"
    PRODUCT_CONFIG_DIR "${CMAKE_SOURCE_DIR}/../platform_config"
    CAN CAN_EVENTS) # 按需要选择 CAN 和事件服务
```

启动时使用 `ABoxMcuClockConfig` 指定实际晶振、PLL 及总线分频，使用 `ABoxBoardUartConfig` 数组指定要初始化的 UART 及完整帧参数，将两者与 App 向量地址传入 `ABoxBoard_Init` 并检查返回值。板级启动不会隐式启动 CAN；需要 CAN 时由产品显式提供 `ABoxCanConfig`。SPI、I2S、I2C、SMBus、ADC、DAC、RTC、USB、USB OTG 信令、外部中断、DMA、定时器、Flash 选项字、窗口看门狗、CRC、备份寄存器、电压检测和引脚复用使用 `abox_platform_attach_<外设>` 按需编入；选项字对应 `flash_options`。编入不会自动配置引脚、NVIC 或启动外设。使用自定义启动文件的产品须自行接入相应中断分发。最小构建见 [F105 接入示例](../examples/f105_app/README.md)。

V1 选择 GD32F105，V2 选择 STM32F105；Boot、链接布局和板上引脚以实际产品为准。公共主机测试和交叉编译不等于实际外设、真实总线、运动或 OTA 验收。

## 接口边界

- 双 ADC 交替注入组合使用规则组同步模式；自动注入使用规则组与注入组均同步的模式。注入结果寄存器只能保存最近一次结果，触发过快或中断处理过慢时可能覆盖。
- 定时器寄存器序列 DMA burst 的可选请求受芯片 DMA 映射约束；直接写入控制寄存器可能使软件缓存失效，恢复方式见 `abox_timer.h`。
- USB 枚举、类协议和 OTG 协商消息由上层实现；公共层提供控制器数据通道及 OTG 角色切换能力。

V3.3 使用 `gd32f105_ec800_v3` 和 `ABOX_BOARD_VERSION=v3`，复用 GD32F105 后端。板级资源、NOR 能力选择与台架验证见 [V3.3 接入与验收](../docs/internal/abox-v3-bringup.md)。
