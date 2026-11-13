"""Compile the STM32 CAN adapter against the platform's pinned HAL headers."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
vendor = root / "vendor/stm32f1/hal-1.1.10"
out = Path(sys.argv[2]) / "stm32f105-can-vendor.o"
includes = [root / "hardware/api/include", root / "tests/stm32_vendor_stub",
            root / "targets/stm32f105_abox_boot/Core/Inc",
            vendor / "STM32F1xx_HAL_Driver/Inc", vendor / "STM32F1xx_HAL_Driver/Inc/Legacy",
            vendor / "CMSIS/Device/ST/STM32F1xx/Include", vendor / "CMSIS/Include"]
command = [sys.argv[1], "-std=c11", "-mcpu=cortex-m3", "-mthumb", "-Wall", "-Wextra", "-Werror",
           "-DUSE_HAL_DRIVER", "-DSTM32F105xC", "-DHAL_CAN_MODULE_ENABLED"] + ["-I" + str(path) for path in includes]
subprocess.run(command + ["-c", str(root / "hardware/mcu/stm32f105/can_port_stm32f105.c"),
                          "-o", str(out)], check=True)
