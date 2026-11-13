# GD32F105 ABox 公共 Boot

内部资料，不作为正式发布产物。该目标适配 GD32F105RCT6、8 MHz 外部晶振的 ABox V1 和 EC800，使用固定版本的 GD 官方固件库。它复用平台 Boot V2 状态机及公共安装器，App 负责 HTTPS 下载、UFS 分槽和校验元数据；Boot 不负责网络下载。

GD Boot 独立标识为 `abox-gd-boot-1.0.0`，与 STM32 Boot 独立编排版本；二者使用相同的描述符 ABI 和 Boot V2 状态格式。描述符位于 `0x08007F00`，在安装/回滚验证完成前能力位为 0；当前构建不能用于正式 OTA 或替换冻结 STM32 Boot。状态页和配置页地址沿用 Boot V2 布局，安装器只擦写 App 区域。

```powershell
cmake -S . -B build/Release -G Ninja '-DCMAKE_BUILD_TYPE=Release' '-DCMAKE_TOOLCHAIN_FILE=gcc-arm-none-eabi.cmake'
cmake --build build/Release
```

当前已验证交叉构建、向量和描述符 CRC、故障邮箱地址以及安装器主机测试。在 V1 上从 RAM 运行该 Boot，成功初始化 EC800/UFS、读取并校验当前稳定槽的 150404 字节镜像，Flash 调用被 RAM 测试保护拦截，随后成功跳回现有 App。独立台架检查已验证从芯片 Flash 启动、COM4 启动日志，以及保留测试页的实际擦除、编程、回读和重新上锁，测试后恢复了原 Boot。使用当前稳定槽重装相同 App 的完整镜像安装已实测通过；候选槽切换、掉电恢复和试运行/回滚尚未通过实测。新 Boot 写入在役板前必须有经核对的迁移和恢复路径；不需要生成设备备份。

链接脚本将 `0x08007000..0x080077FF` 留作受控台架测试页，禁止 Boot 代码和数据占用。仅专用测试构建可使用该页；写入前须核对页内预期填充值，并在测试后从已核对的原 Boot 制品恢复该页。普通 Boot 不访问该页。
