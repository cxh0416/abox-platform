# F105 公共平台最小接入示例

此示例用于演示 CMake 接入并验证构建，不包含产品业务、MQTT 配置或可交付固件。内存布局及 Boot V2 邮箱沿用当前 256 KiB F105 板基线；新产品使用前必须按实际芯片、Boot 和分区核对。示例的启动、syscalls 和链接脚本源于送餐车现有配置。

在平台仓库根目录执行（需要 `arm-none-eabi-gcc`、CMake 和 Ninja）：

```powershell
cmake -S examples/f105_app -B build/examples/f105-gd -G Ninja '-DCMAKE_BUILD_TYPE=Release' '-DCMAKE_TOOLCHAIN_FILE=../../cmake/toolchains/gcc-arm-none-eabi-f105.cmake' '-DABOX_HARDWARE=gd32f105_ec800_v1' '-DABOX_BOARD_VERSION=v1'
cmake --build build/examples/f105-gd

cmake -S examples/f105_app -B build/examples/f105-st -G Ninja '-DCMAKE_BUILD_TYPE=Release' '-DCMAKE_TOOLCHAIN_FILE=../../cmake/toolchains/gcc-arm-none-eabi-f105.cmake' '-DABOX_HARDWARE=stm32f105_ec800_v1' '-DABOX_BOARD_VERSION=v2'
cmake --build build/examples/f105-st
```

示例默认启用公共 CAN Port 并在启动后初始化通道 0 的静默回环，不发送真实总线帧。`-DABOX_EXAMPLE_CAN=OFF` 不初始化 CAN。普通项目应按自己的业务配置、链路和所选 Boot 验证，不要直接刷写本示例。

可选外设使用相同 `HARDWARE` 参数，在 `abox_platform_attach_f105_app` 之后按需调用 `abox_platform_attach_spi`、`abox_platform_attach_i2c`、`abox_platform_attach_adc`、`abox_platform_attach_dac`、`abox_platform_attach_exti`、`abox_platform_attach_timer`、`abox_platform_attach_rtc` 和 `abox_platform_attach_dma`。应用仍需配置引脚、IRQ 与触发源，并排除其他代码对 DMA 和外设的直接占用。设置 `-DABOX_EXAMPLE_EXTRA_HAL=ON` 将这些 API 全部编入并强制链接，用于验证双芯片构建，不会自动启动它们。

另设 `-DABOX_EXAMPLE_USB=ON`，用 `abox_platform_attach_usb` 将 USB 控制器及对应厂商驱动编入并强制链接。`ABOX_EXAMPLE_EXTRA_HAL=ON` 还通过 `abox_platform_attach_usb_otg` 编入 OTG 信令和自动角色接管，因此也会编入 USB 后端。示例不会配置 USB 引脚或供电，也不会启动控制器；实际设备的时钟、VBUS、引脚、IRQ 和上层 USB 协议栈由产品负责。

## V3.3 参考构建

```powershell
cmake -S examples/f105_app -B build/examples/f105-v3 -G Ninja '-DCMAKE_BUILD_TYPE=Release' '-DCMAKE_TOOLCHAIN_FILE=../../cmake/toolchains/gcc-arm-none-eabi-f105.cmake' '-DABOX_HARDWARE=gd32f105_ec800_v3' '-DABOX_BOARD_VERSION=v3' '-DABOX_EXAMPLE_NOR_IDEM=ON' '-DABOX_EXAMPLE_NOR_OTA=ON'
cmake --build build/examples/f105-v3 --target ABoxF105Example
cmake --build build/host
ctest --test-dir build/host -R 'gd32f105_v3_board|idem_nor|boot_v2_app|f105_example_build|gd32f105_boot_build' --output-on-failure
```

防重和 OTA 两个开关均默认关闭，可独立启用。参考 App 通过 SWD 暴露 `g_abox_example_nor_id`、`g_abox_example_idem_ready` 和 `g_abox_example_nor_boot_matches`。参考 App 的 OTA 配置由产品注入，构建选项用于链接和配套 Boot 能力检查。

防重容量可用 `-DABOX_NOR_IDEM_KIB=1536` 配置，单位为 KiB，默认保留 1.5 MiB 防重和 2 MiB 未分配空间。

台架专用选项 `ABOX_EXAMPLE_NOR_PROBE=ON` 在每次启动时擦写并恢复 NOR 的 `0x07F000–0x07FFFF`，跨页写入的测试结果见 `g_abox_example_nor_probe`，1 为通过，2 为失败。板级接入见 [V3.3 接入与验收](../../docs/internal/abox-v3-bringup.md)。
