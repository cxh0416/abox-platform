"""Check that only the GD FreeRTOS target requires scheduler vector handlers."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
gd = root / "hardware/mcu/gd32f105"
vendor = root / "vendor/gd32f10x/2.7.0"
build = Path(sys.argv[1])
for enabled in (False, True):
    obj = build / ("gd32-freertos-vectors.o" if enabled else "gd32-baremetal-vectors.o")
    command = ["arm-none-eabi-gcc", "-mcpu=cortex-m3", "-mthumb", "-std=gnu11",
               "-DGD32F10X_CL", "-I" + str(gd / "include"),
               "-I" + str(vendor / "CMSIS"), "-I" + str(vendor / "CMSIS/GD/GD32F10x/Include"),
               "-I" + str(vendor / "GD32F10x_standard_peripheral/Include"),
               "-I" + str(root / "components/boot_v2/include"),
               "-c", str(gd / "startup_gd32f105.c"), "-o", str(obj)]
    if enabled:
        command.insert(1, "-DABOX_GD_FREERTOS=1")
    subprocess.run(command, check=True)
    relocations = subprocess.check_output(["arm-none-eabi-objdump", "-r", str(obj)], text=True)
    for handler in ("SVC_Handler", "PendSV_Handler", "SysTick_Handler"):
        assert (handler in relocations) == enabled, handler
print("GD bare-metal and FreeRTOS startup vectors verified")
