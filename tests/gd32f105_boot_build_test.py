"""Build and inspect the GD Boot before hardware validation without flashing hardware."""
import pathlib
import struct
import subprocess
import sys
import zlib

root = pathlib.Path(__file__).resolve().parents[1]
source = root / "targets/gd32f105_abox_boot"
build = pathlib.Path(sys.argv[1]) / "gd32-boot-contract"
subprocess.run(["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
                "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_TOOLCHAIN_FILE=" + str(source / "gcc-arm-none-eabi.cmake")], check=True)
subprocess.run(["cmake", "--build", str(build)], check=True)
image = (build / "ABox_GD_Boot.bin").read_bytes()
assert len(image) == 0x8000, len(image)
assert image[0x7000:0x7800] == bytes(0x800), "reserved test page must remain padding"
sp, pc = struct.unpack_from("<II", image)
assert sp == 0x20010000 and pc & 1 and 0x08000000 <= (pc & ~1) < 0x08007F00
desc = image[0x7F00:]
magic, abi, length, features = struct.unpack_from("<IHHI", desc)
assert (magic, abi, length, features) == (0x32564241, 1, 256, 15)
assert struct.unpack_from("<HHH", desc, 12) == (2, 3, 0)
assert desc[20:52].split(b"\0", 1)[0] == b"abox_gd32f105"
assert desc[52:116].split(b"\0", 1)[0] == b"abox-boot-2.3.0-gd"
assert zlib.crc32(desc[:252]) == struct.unpack_from("<I", desc, 252)[0]
subprocess.run([sys.executable, str(source / "tools/verify_boot_artifact.py"),
                str(build / "ABox_GD_Boot.bin")], check=True)
symbols = subprocess.check_output(["arm-none-eabi-nm", "-n", str(build / "ABox_GD_Boot")], text=True)
assert not any(" HAL_" in line for line in symbols.splitlines())
assert any(line.startswith("2000bc40 ") and line.endswith(" g_fault") for line in symbols.splitlines())
ram = pathlib.Path(sys.argv[1]) / "gd32-boot-ram-contract"
subprocess.run(["cmake", "-S", str(source), "-B", str(ram), "-G", "Ninja",
                "-DCMAKE_BUILD_TYPE=Release", "-DABOX_GD_BOOT_RAM_SMOKE=ON",
                "-DCMAKE_TOOLCHAIN_FILE=" + str(source / "gcc-arm-none-eabi.cmake")], check=True)
subprocess.run(["cmake", "--build", str(ram)], check=True)
ram_image = (ram / "ABox_GD_Boot_Bench.bin").read_bytes()
ram_sp, ram_pc = struct.unpack_from("<II", ram_image)
assert ram_sp == 0x20010000 and ram_pc & 1 and 0x20000000 <= (ram_pc & ~1) < 0x20004F00
ram_symbols = subprocess.check_output(["arm-none-eabi-nm", str(ram / "ABox_GD_Boot_Bench")], text=True)
for name in ("ABoxGd32Flash_Begin", "ABoxGd32Flash_Write", "ABoxGd32Flash_ErasePage"):
    assert any(line.endswith(" __wrap_" + name) for line in ram_symbols.splitlines())
    assert not any(line.endswith(" " + name) for line in ram_symbols.splitlines())
print("GD Boot vectors, reserved mailbox, descriptor CRC and shared feature flags verified")
