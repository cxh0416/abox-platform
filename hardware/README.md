# ABox 硬件库

公共平台负责硬件接口、MCU 后端、板卡配置和固定版本的厂商库。业务代码使用 ABox 接口；产品仍负责业务逻辑、协议 Profile、配置以及各产品特有的外设使用方式。

```text
hardware/
├── api/                 # 串口、EC800 串口、CAN、MCU 身份等公共接口
├── mcu/
│   ├── gd32f105/        # GD32F105 驱动、启动和外设中断
│   └── stm32f105/       # STM32F105 的 ABox 接口适配
└── boards/               # GD V1、ST V2 板级启动和引脚配置
vendor/                  # 固定版本厂商库及许可
targets/                 # 板级硬件合同、独立 Boot 工程
tests/hardware/          # 独立硬件检查目标
```

STM32 板级启动和 V2 输入引脚表在 `boards/abox_v2/`；兼容既有产品的板载 IO 适配和历史硬件合同仍在 `targets/stm32f105_ec800_v1/`。V2 的四路新增输入由 `ABOX_BOARD_VERSION` 映射支持；合同名称后缀 `v1` 不是 PCB 版本。

## 接口与产品接入

- `abox_serial_port.h`：产品串口收发与回调；回调在中断中执行，异步发送缓冲区在完成或取消前必须保持有效。停止传输并反初始化后可设置 8N1 波特率再初始化；GD UART4 按实际 APB1 时钟及 16 倍采样的 16 位分频范围校验，默认 115200，也支持 19200 等可表示的速率。实际波特率由硬件分频取整，需按外设容差核对。
- `abox_serial_transaction.h`：可选单端口请求/应答接收队列；发送前开始接收，保留空闲线分段并在溢出或接收错误时报告失败。使用 `abox_platform_attach_serial_transaction` 接入，产品任务负责超时与协议解析。
- `abox_modem_port.h`：EC800 串口及循环 DMA 接收位置通知；缓冲区由调用方持有。
- `abox_can_port.h`：可选 CAN 轮询接口，发送成功仅表示进入发送队列，限制见[CAN 说明](../docs/internal/can-port.md)。
- `abox_mcu.h`、`abox_target_identity.h`：芯片 UID 与目标身份。
- Flash、单调时间和临界区复用 `ABoxPlatformPort`；板载 GPIO 使用 `ABoxBoardIoPort`。按外部端子使用 `ABoxBoardIo_ReadTerminal`；不可用的端子返回失败，不修改输出值。

对于符合现有 F105/EC800 板卡合同的新产品，先 `add_subdirectory` 引入平台，然后对产品可执行目标调用一次：

```cmake
abox_platform_attach_f105_app(MyApp
    HARDWARE "${ABOX_HARDWARE}"
    PRODUCT_CONFIG_DIR "${CMAKE_SOURCE_DIR}/../platform_config"
    CAN) # 不使用公共 CAN 时可省略；STM32 将沿用旧 CAN 启动
```

该入口负责厂商库、板级启动、中断、标准 App 组件、串口、EC800 串口和身份适配；`CAN` 可选。默认使用平台固定的 STM32 HAL 配置，如确需覆盖可传 `HAL_CONFIG_DIR`。产品仍需提供 App 源码、`boot_cfg.h`、syscalls、符合所选 Boot 的链接脚本、产品身份和业务配置；App 启动时调用 `ABoxBoard_Init` 并检查返回值。已有底层 CMake 入口保留给不同需求的产品，不要与此入口重复接入同一功能。最小完整构建见 [F105 接入示例](../examples/f105_app/README.md)，现有产品见送餐车 `app/CMakeLists.txt`。

STM32 板级启动及 CubeMX 外设、中断源文件由平台提供；产品不能再把另一份 HAL、启动或板级源码链接进同一个目标。冻结 STM32 Boot 保留自己的既有构建输入。产品目录保留的旧 CubeMX 文件不参与送餐车当前构建，重新生成代码时须同步核对平台板级文件。

GD 和 STM32 Boot 分别构建、分别编排版本，使用公共 Boot V2 描述符和状态格式。GD Boot 的当前实测范围及 OTA 使用限制见 [GD Boot 说明](../targets/gd32f105_abox_boot/README.md)；更完整的板卡、构建和台架记录见[硬件架构](../docs/internal/hardware-architecture.md)。

## 送餐车构建选择

在送餐车仓库的 `app/` 中执行，两个目录分别保存不同目标的构建结果：

```powershell
cmake -S . -B build/GdV1 -G Ninja '-DCMAKE_BUILD_TYPE=Release' '-DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake' '-DABOX_HARDWARE=gd32f105_ec800_v1' '-DABOX_BOARD_VERSION=v1'
cmake --build build/GdV1

cmake -S . -B build/Release -G Ninja '-DCMAKE_BUILD_TYPE=Release' '-DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake' '-DABOX_HARDWARE=stm32f105_ec800_v1' '-DABOX_BOARD_VERSION=v2'
cmake --build build/Release
```

两种配置选择相同的送餐车业务源文件，通过平台选择板级启动、外设和厂商库。送餐车默认保留 STM32 原有 CAN 启动；传 `-DABOX_USE_CAN_PORT=ON` 则让公共 CAN Port 接管，产品需要自行调用 `ABoxCanPort_Init`。它们生成各自的 App；Boot 独立构建。仅凭构建成功不能确认物理接线或 OTA 功能。
