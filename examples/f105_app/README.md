# F105 公共平台最小接入示例

此示例用于演示 CMake 接入并验证构建，不包含产品业务、MQTT 配置或可交付固件。内存布局及 Boot V2 邮箱沿用当前 256 KiB F105 板基线；新产品使用前必须按实际芯片、Boot 和分区核对。示例的启动、syscalls 和链接脚本源于送餐车现有配置。

在平台仓库根目录执行（需要 `arm-none-eabi-gcc`、CMake 和 Ninja）：

```powershell
cmake -S examples/f105_app -B build/examples/f105-gd -G Ninja '-DCMAKE_BUILD_TYPE=Release' '-DCMAKE_TOOLCHAIN_FILE=../../cmake/toolchains/gcc-arm-none-eabi-f105.cmake' '-DABOX_HARDWARE=gd32f105_ec800_v1' '-DABOX_BOARD_VERSION=v1'
cmake --build build/examples/f105-gd

cmake -S examples/f105_app -B build/examples/f105-st -G Ninja '-DCMAKE_BUILD_TYPE=Release' '-DCMAKE_TOOLCHAIN_FILE=../../cmake/toolchains/gcc-arm-none-eabi-f105.cmake' '-DABOX_HARDWARE=stm32f105_ec800_v1' '-DABOX_BOARD_VERSION=v2'
cmake --build build/examples/f105-st
```

示例默认启用公共 CAN Port 并在启动后初始化通道 0 的静默回环，不发送真实总线帧。`-DABOX_EXAMPLE_CAN=OFF` 会恢复 STM32 现有的 CAN 启动配置。普通项目应按自己的业务配置、链路和所选 Boot 验证，不要直接刷写本示例。
