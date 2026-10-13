# A-Box 公共固件平台

当前平台版本为 `0.3.0`。平台同时提供可复用固件组件，以及面向统一 STM32F105/EC800 硬件基线的完整 Boot V2 工程。

## 目录说明

- `components/core/include/`、`components/core/src/`：产品配置校验、硬件回调端口和平台版本接口。
- `components/board_io/`：可选的通用板载 I/O 状态组件；板型映射见 [内部说明](docs/internal/board_io.md)。
- `components/boot_v2/`：Boot State V3、描述符、故障邮箱、App 下载端和 Boot 安装端。
- `components/ota/`：当前 HTTPS/UFS 下载器；旧架构 EC800 AT 与直接写 Flash 组件位于 `components/legacy/ota/`。
- `components/ec_power/`：EC800 上电、关机和重启时序。
- `components/cjson/`：平台统一使用的 cJSON。
- `cmake/`：产品 App、旧 Boot 和 Boot V2 的 CMake 接入函数。
- `targets/stm32f105_abox_boot/`：完整、可独立构建的公共 Boot 工程。
- `tests/`：公共组件主机测试。
- `tools/`：Flash 布局、产品矩阵、基线和产品发布校验工具。
- [底层 SWD 调试工具](docs/internal/jlink_swd_debugging.md)：常规 J-Link/GDB 连续读取失败时使用公共脚本切换到 CoreSight AP/DP 访问。
- `docs/`：平台合同、公共 MQTT V3/V4 协议、V4 Registry、产品迁移矩阵和基线记录。

编译缓存统一放在根目录的 `build/` 下：主机测试复用 `build/host/`，公共 Boot 复用 `build/boot/Release/`（或 `Debug/`）。这些目录不属于平台源码。

## 公共 Boot

公共 Boot 目标适用于 `STM32F105RCT6 + EC800` 的 `abox_stm32f105_ec800_v1` 硬件合同。当前冻结版本为 `abox-boot-2.3.0`：

- Boot：`0x08000000..0x08007FFF`；
- 描述符：`0x08007F00..0x08007FFF`；
- App：`0x08008000..0x0803E7FF`；
- Boot State A/B：`0x0803E800`、`0x0803F000`；
- 产品配置页：`0x0803F800`；
- 冻结产物：`targets/stm32f105_abox_boot/dist/ABox_Boot.bin`；
- SHA-256：`981409ef107d5f5c56a1a80fce107c0292e2c1165668c0279ba6158f24a61c63`。

Boot 不连接业务平台，也不下载网络固件。产品 App 负责 HTTPS、CA、EC800 UFS 和 OTA Topic；Boot 只负责安装、试运行确认和回滚。

## 产品接入

产品 App 通过 Git 子模块固定平台提交，并在自身 CMake 中调用：

```cmake
add_subdirectory("${CMAKE_SOURCE_DIR}/../platform" abox_platform_build)
abox_platform_attach(Product_App)
abox_platform_attach_boot_v2_app(Product_App)
abox_platform_attach_standard_port(Product_App HARDWARE stm32f105_ec800_v1)
```

标准 Port 接入为可选项，仅适用于引脚和 EC800 电源接线符合 `stm32f105_ec800_v1` 的 App；其他板卡提供自己的 Hardware Contract/Port。它使用产品 `boot_cfg.h` 的 Flash 布局，不定义输出的业务含义。当前 Platform 已有 MQTT V4 Registry、transport/runtime、Enrollment 和 Boot V2 基础组件；Cloud 编排、产品 Profile/状态/业务及发布包装仍有产品侧代码。新项目接入与迁移门槛见[内部实施说明](docs/internal/new_product_onboarding.md)。产品仓库不得复制公共 MQTT 协议，也不得复制或局部修改公共 Boot。

正式公共基线为 [MQTT V4](docs/protocols/A-BOX设备公共MQTT协议-V4.0.md)；[MQTT V3](docs/protocols/A-BOX设备公共MQTT协议-V3.0.md) 仅供尚未整批迁移的存量项目使用。V4 设备单版本运行，跨项目迁移期间由平台同时保留 V3/V4 Handler。

V4 Core 命令和已冻结 Profile 的唯一机器可校验登记入口是 [V4 Registry](docs/protocols/v4/registry.json)。运行 `python tools/validate_mqtt_v4.py` 可校验 Registry 和 Canonicalization/SHA-256 固定向量。

当前 `Top_Flying_Wing`、`LockCtrlBoard_cheweishi`、`Meal_Delivery_Vehicle`、`Sweeper_VCU` 已接入公共 Boot；`Airport_vehicle`、`CageDumper` 仍为旧 Boot 网络下载架构，切换前必须先改造 App，并确认旧设备迁移方案。

## 构建与验证

```powershell
cmake -S . -B build/host -G Ninja -DBUILD_TESTING=ON
cmake --build build/host
ctest --test-dir build/host --output-on-failure

powershell -ExecutionPolicy Bypass -File .\targets\stm32f105_abox_boot\tools\build_release.ps1
python .\targets\stm32f105_abox_boot\tools\verify_boot_artifact.py `
  .\targets\stm32f105_abox_boot\dist\ABox_Boot.bin
```

构建、主机测试和 MQTT 确认都不能替代真实设备上的 Boot 跳转、UFS 安装、断电恢复、试运行确认和回滚验收。
