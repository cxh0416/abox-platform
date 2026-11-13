"""Cross-compile the ABox FMC adapter against its pinned vendor source."""

import pathlib
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parents[1]
out = pathlib.Path(sys.argv[2]) / "gd32f105-vendor-compile"
out.mkdir(parents=True, exist_ok=True)
vendor = root / "vendor/gd32f10x/2.7.0"
adapter = root / "hardware/mcu/gd32f105"
includes = [adapter / "include", vendor / "CMSIS", root / "hardware/api/include",
            root / "components/core/include",
            root / "hardware/boards/abox_v1/include", root / "components/board_io/include",
            vendor / "CMSIS/GD/GD32F10x/Include",
            vendor / "GD32F10x_standard_peripheral/Include"]
common = [sys.argv[1], "-std=c11", "-mcpu=cortex-m3", "-mthumb", "-DGD32F10X_CL"]
common += ["-I" + str(path) for path in includes]
sources = [(adapter / "flash_gd32f105.c", ["-Wall", "-Wextra", "-Werror"]),
           (adapter / "board_io_gd32f105_v1.c", ["-Wall", "-Wextra", "-Werror"]),
           (adapter / "serial_port_gd32f105.c", ["-Wall", "-Wextra", "-Werror"]),
           (adapter / "modem_port_gd32f105.c", ["-Wall", "-Wextra", "-Werror"]),
           (adapter / "monotonic_gd32f105.c", ["-Wall", "-Wextra", "-Werror"]),
           (adapter / "mcu_gd32f105.c", ["-Wall", "-Wextra", "-Werror"]),
           (adapter / "can_port_gd32f105.c", ["-Wall", "-Wextra", "-Werror"]),
           (vendor / "GD32F10x_standard_peripheral/Source/gd32f10x_fmc.c", []),
           (vendor / "GD32F10x_standard_peripheral/Source/gd32f10x_gpio.c", []),
           (vendor / "GD32F10x_standard_peripheral/Source/gd32f10x_rcu.c", []),
           (vendor / "GD32F10x_standard_peripheral/Source/gd32f10x_usart.c", []),
           (vendor / "GD32F10x_standard_peripheral/Source/gd32f10x_dma.c", []),
           (vendor / "GD32F10x_standard_peripheral/Source/gd32f10x_timer.c", []),
           (vendor / "GD32F10x_standard_peripheral/Source/gd32f10x_can.c", []),
           (vendor / "GD32F10x_standard_peripheral/Source/gd32f10x_dbg.c", [])]
for source, warnings in sources:
    subprocess.run(common + warnings + ["-c", str(source), "-o", str(out / (source.stem + ".o"))],
                   check=True)
