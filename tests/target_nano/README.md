# F105 目标库整数序列化回归

在 ABox Platform 根目录运行以下 PowerShell 命令。使用与正式 App 相同的 Arm GNU Toolchain，
Cortex-M3 Thumb、`-Os` 和 newlib-nano。检查生成的 map 中的 `thumb/v7-m/nofp`、
`nano-svfprintf.o` 和工具链版本与待验 App 的 map 一致。

各产品 CI 可从统一的平台检出运行 `python tools/check_firmware_serialization.py --product-root <产品根目录>`。
多产品任务可重复提供 `--product-root`。扫描覆盖公共源码及所选产品的 app、boot、shared 和 platform_config，
跳过测试和第三方目录。此检查也能用于固定在旧平台版本的产品。

```powershell
$platformRoot = (Resolve-Path .).Path
$toolchain = "$platformRoot/cmake/toolchains/gcc-arm-none-eabi-f105.cmake"
cmake -S tests/target_nano -B build/target-nano -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$toolchain" -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build build/target-nano --target abox_nano_smoke
```

测试执行生产编码器，检查 `0`、`UINT64_MAX`、缓冲区不足、内部 V2 响应中的
`1731912345678`，以及公共 V4 的 `timestamp` 和 canonical `expiresAt` 安全整数上界。
目标用例逐字节比较完整 JSON。主机 CTest 继续读取[冻结向量](../../docs/protocols/v4/test-vectors.json)，覆盖小数、指数和负零等词法。

### 运行 Arm 镜像

在有 Python 和 Unicorn 2.1 的测试环境中运行下列命令。模拟器执行镜像内链接的目标库。
脚本遇到断言、异常或未到达结束断点时以非零状态退出，适合独立的 CI 步骤。

```powershell
python -m pip install "unicorn>=2.1,<3"
python tests/target_nano/run_image.py build/target-nano/abox_nano_smoke.bin
```

也可在具有 64 KiB SRAM 的 F105 台架通过 J-Link GDB Server 执行该 ELF。
以下命令在独占测试板的调试会话中复位并接管 SRAM，测试结束后复位以启动原固件。
连接已启动的 GDB Server，使用 `arm-none-eabi-gdb build/target-nano/abox_nano_smoke` 打开 ELF。

```gdb
target remote localhost:2331
monitor reset
load
set $sp = *(unsigned int *)0x20000000
set $pc = *(unsigned int *)0x20000004 & ~1
continue
p/d $r0
monitor reset
detach
```

镜像在 `BKPT #0` 停止。R0 为 0 表示全部通过，断言失败时 R0 是测试源文件的行号。
台架镜像、Host CTest 和产品正式固件分别构建。

### CageDumper 的事故用例

在 CageDumper 根目录运行。产品的 `platform` 子模块提供待验生产代码。
可用相邻 ABox Platform 检出的 SRAM 测试入口单独验证已固定的平台版本。

```powershell
$benchPlatform = (Resolve-Path ../ABox_Platform).Path
$toolchain = "$benchPlatform/cmake/toolchains/gcc-arm-none-eabi-f105.cmake"
cmake -S tests/cage_manual_v4 -B app/build/target-nano -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$toolchain" "-DABOX_V4_PLATFORM_DIR=$PWD/platform" "-DABOX_NANO_BENCH_DIR=$benchPlatform/tests/hardware" -DCMAKE_BUILD_TYPE=MinSizeRel
cmake --build app/build/target-nano --target cage_ota_test
python "$benchPlatform/tests/target_nano/run_image.py" app/build/target-nano/cage_ota_test.bin
```

产品用例从内部 V2 请求进入 OTA 消息处理，注入设备时钟，检查待发槽、完整 JSON、
发送就绪后的发布参数及应答确认。Boot 和模组接口使用测试替身。
实机验收另取本轮台架的 VID、固件版本和平台自动 `get_info` 探测的 requestId，
用产品的 `tests/cage_manual_v4/verify_get_info_trace.py` 对平台已有追踪做只读复核。
其命令行为 `python verify_get_info_trace.py <追踪库> <VID> <requestId> <固件版本>`。
通过条件包含对应的请求与响应、发送及接收 Topic、成功码，以及响应 JSON 中超过 32 位的整数时间戳。
