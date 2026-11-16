# GD32F105 ABox 公共 Boot

内部资料，不作为业务对接协议对外交付。该目标适配 GD32F105RCT6、8 MHz 外部晶振的 ABox V1 和 EC800，使用固定版本的 GD 官方固件库。它复用平台 Boot V2 状态机及公共安装器，App 负责 HTTPS 下载、UFS 分槽和校验元数据；Boot 不负责网络下载。

GD Boot 标识为 `abox-boot-2.3.0-gd`，与 STM32 Boot 使用相同的版本主体，并通过后缀区分芯片固件库；二者使用相同的描述符 ABI 和 Boot V2 状态格式。描述符位于 `0x08007F00`，按复用的公共 Boot V2 实现声明能力位；状态页和配置页地址沿用 Boot V2 布局，安装器只擦写 App 区域。

按产品维护决定，该版本作为 V1/GD32 的冻结基线；固定制品为 [ABox_Boot.bin](dist/ABox_Boot.bin)，长度及 SHA-256 以 [manifest.json](dist/manifest.json) 为准，当前 SHA-256 为 `3eb4df8bda5c8a99c338dfe93a289acd43a4b94bf432b5785ec10ea90340f237`。冻结基线与 V2/STM32 的冻结 Boot 是两个不同制品，不得混用。冻结不改变已有实机验证范围。

```powershell
$target = (Resolve-Path targets/gd32f105_abox_boot).Path
cmake -S $target -B build/boot-gd32/Release -G Ninja '-DCMAKE_BUILD_TYPE=Release' "-DCMAKE_TOOLCHAIN_FILE=$target/gcc-arm-none-eabi.cmake"
cmake --build build/boot-gd32/Release
python "$target/tools/verify_boot_artifact.py" build/boot-gd32/Release/ABox_GD_Boot.bin
```

上述命令在公共平台仓库根目录执行，只生成待核对的构建输出，不覆盖冻结制品。

当前已验证交叉构建、向量和描述符 CRC、故障邮箱地址以及安装器主机测试。早期 `abox-gd-boot-1.0.0` 台架实现已在 V1 上从 RAM 和 Flash 启动，完成 EC800/UFS 初始化、稳定槽校验、保留测试页实际擦写和回读，以及从稳定槽重装相同 App 后跳转。当前 `2.3.0-gd` 冻结制品没有完整候选槽切换、掉电恢复和试运行/回滚实机记录；新 Boot 写入在役板前须核对迁移和恢复路径。

链接脚本将 `0x08007000..0x080077FF` 留作受控台架测试页，禁止 Boot 代码和数据占用。仅专用测试构建可使用该页；写入前须核对页内预期填充值，并在测试后从已核对的原 Boot 制品恢复该页。普通 Boot 不访问该页。
