# J-Link/GDB 读取失败时的底层 SWD 调试

入口：[tools/jlink_swd.py](../../tools/jlink_swd.py)。适用于 STM32F105 / Cortex-M3、AHB-AP0 和 FPB v1；只依赖 Python 3 标准库及已安装的 SEGGER J-Link 动态库。Linux 默认库路径为 `/opt/SEGGER/JLink/libjlinkarm.so`，其他路径通过 `--library` 指定。

启动故障先检查初始化顺序、依赖就绪条件和失败返回值。常规 J-Link/GDB 连续读取失败且没有新增证据时，直接使用此脚本，不继续循环连接和复位。它通过 `JLINKARM_CORESIGHT_ReadAPDPReg` / `WriteAPDPReg` 访问 AP/DP，不调用常规 `ReadMem`、`ReadRegs` 或 GDB。

## 首次放到调试机

在公共平台仓库根目录执行以下命令。`wyse3040` 使用现有 SSH 别名；脚本没有内置服务器地址、产品 VID 或设备 UID。

```powershell
ssh wyse3040 'mkdir -p ~/abox-tools'
scp tools/jlink_swd.py wyse3040:abox-tools/jlink_swd.py
ssh wyse3040 'python3 ~/abox-tools/jlink_swd.py status'
```

关闭占用同一探针的 J-Link Commander/GDB Server，再运行脚本。使用 `status` 返回的 24 位 `uid`，按 STM32 三个 32 位 UID 字的小端读取、逐字大写十六进制排列，例如本次底盘的 `05D3FF343733574E43043550`。改变核状态的命令必须指定 `--expect-uid`，不匹配时不执行后续操作。

## 常用命令

下列命令在调试机执行。替换 `UID_FROM_STATUS`；全局参数放在子命令前。

```sh
# 默认只读；不停止 CPU、不复位、不生成捕获文件。
python3 ~/abox-tools/jlink_swd.py status
python3 ~/abox-tools/jlink_swd.py --expect-uid UID_FROM_STATUS read 0x08008000 32

# 暂停读寄存器 / 故障现场，退出时恢复进入时的运行或停核状态。
python3 ~/abox-tools/jlink_swd.py --expect-uid UID_FROM_STATUS regs
python3 ~/abox-tools/jlink_swd.py --expect-uid UID_FROM_STATUS fault

# 等待下一次 HardFault；超时后恢复状态，不自动复位或重新连接。
python3 ~/abox-tools/jlink_swd.py --expect-uid UID_FROM_STATUS capture --timeout 30

# 明确复位一次，从干净启动状态捕获 HardFault。
python3 ~/abox-tools/jlink_swd.py --expect-uid UID_FROM_STATUS capture --reset --timeout 30

# 将 INSTRUCTION_ADDRESS_FROM_ELF 替换为本次 ELF 中指令起点的 0x 地址。
python3 ~/abox-tools/jlink_swd.py --expect-uid UID_FROM_STATUS capture --breakpoint INSTRUCTION_ADDRESS_FROM_ELF --timeout 30

# 仅在需要清除遗留调试状态时使用：清所有 FPB 指令比较器，移除
# 复位/HardFault 捕获和 STM32F105 看门狗冻结，恢复 CPU 运行并禁用调试。
python3 ~/abox-tools/jlink_swd.py --expect-uid UID_FROM_STATUS cleanup
```

`capture` 在武装后立即输出 `stage=armed`，命中后输出寄存器、SCB 故障寄存器，以及 LR 为合法 EXC_RETURN 且栈地址在 STM32F105 64 KiB RAM 内时的异常栈帧。`exception_frame.pc` 是异常前 PC；`registers.pc` 是当前停止位置。JSON 中寄存器和地址为整数，可用 `hex()` 转换后配合同一固件的 ELF / `arm-none-eabi-addr2line` 定位。

| 参数 / 行为 | 约定 |
|---|---|
| `--speed` | 默认 1000 kHz；通过 J-Link 选择 SWD |
| `--io-timeout` | 停核、寄存器传输及复位接管等待上限，默认 1 秒 |
| `capture --timeout` | 等待异常/断点，默认 10 秒；上限 600 秒 |
| `read ADDRESS SIZE` | 支持非对齐区间；32 位小端读取，跨 1 KiB 重新设置 TAR；单次最大 1 MiB |
| `read ... --output PATH` | 显式输出二进制文件，拒绝覆盖；默认只向标准输出打印十六进制 |
| `capture --reset` | 唯一会请求 MCU 复位的参数；没有自动重试或 Boot 跳 App 功能 |
| `regs` / `fault` / `capture` | 临时保存并恢复 DEMCR、DBGMCU_CR 和核控制状态；仅恢复脚本占用的 FPB 比较器，保留原有断点 |
| 退出状态 | 0 成功，2 操作/超时/恢复失败，130 用户中断；参数错误由 argparse 返回 2 |

## 实现和使用边界

- DP SELECT=0，AP CSW=`0x23000052` 用于递增读取；单字写调试寄存器使用 `0x23000002`。所有 DAP 返回值均检查，访问失败立即退出。
- CPU 寄存器通过 DCRSR/DCRDR 读取，等待 DHCSR.REGRDY；FPB v1 比较器地址掩掉 bit1，区分高低半字。脚本不提供 Flash 编程、任意内存写入或配置/Boot State 修改命令。
- 临时使用 STM32F105 DBGMCU_CR 的 `0x307` 设置，避免停核期间看门狗继续计时；退出时恢复原值。其他 MCU 不直接套用此板型设置。
- 超时、Ctrl+C 和 SIGTERM 都执行恢复并关闭连接；恢复失败明确报错，仍尝试恢复 CPU 状态。强制终止、断线或原生库调用卡住不能保证执行清理；恢复访问后使用 `cleanup`。等待超时约束 Python 轮询，不限制 SEGGER 原生函数自身的阻塞时间。
- 不从 Boot 手动跳转 App，避免残留时钟状态引入伪故障。是否存在 NRST 接线不改变默认不复位行为；无 NRST 的接管结果需在对应设备上验证。
- 状态恢复以连接后读取的状态为基准；SEGGER 连接本身可能启用 C_DEBUGEN。`status` / `read` 不主动停核、复位或修改看门狗冻结设置。
- 测试：`python tests/test_jlink_swd.py`，或公共平台 `ctest --test-dir build/host -C Debug -R jlink_swd --output-on-failure`。主机模型覆盖 TAR 边界、错误返回、超时、状态恢复及异常帧；实机只读检查与启动捕获分别验证。
