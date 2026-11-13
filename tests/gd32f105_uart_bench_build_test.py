"""Build RAM-only V1 UART and silent CAN bench targets; do not access hardware."""
import pathlib
import struct
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parents[1]
source = root / "tests/hardware"
build = pathlib.Path(sys.argv[1]) / "gd32-uart-bench-contract"
toolchain = root / "targets/gd32f105_abox_boot/gcc-arm-none-eabi.cmake"
subprocess.run(["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
                "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_TOOLCHAIN_FILE=" + str(toolchain)], check=True)
subprocess.run(["cmake", "--build", str(build)], check=True)
image = (build / "gd32_v1_uart_loopback.bin").read_bytes()
sp, pc = struct.unpack_from("<II", image)
assert len(image) < 0xE000 and sp == 0x20010000
assert pc & 1 and 0x20000000 <= (pc & ~1) < 0x2000E000
symbols = subprocess.check_output(["arm-none-eabi-nm", str(build / "gd32_v1_uart_loopback")], text=True)
assert "g_uart_bench_ready" in symbols and "UART4_IRQHandler" in symbols
can_image = (build / "gd32_v1_can_loopback.bin").read_bytes()
sp, pc = struct.unpack_from("<II", can_image)
assert len(can_image) < 0xE000 and sp == 0x20010000
assert pc & 1 and 0x20000000 <= (pc & ~1) < 0x2000E000
can_symbols = subprocess.check_output(["arm-none-eabi-nm", str(build / "gd32_v1_can_loopback")], text=True)
assert "g_can_bench_result" in can_symbols and "ABoxCanPort_Init" in can_symbols
print("GD V1 UART and silent CAN bench images use RAM vectors")
