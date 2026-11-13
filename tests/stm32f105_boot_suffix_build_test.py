"""Build the STM32 suffix candidate without changing the frozen Boot."""
import pathlib
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parents[1]
source = root / "targets/stm32f105_abox_boot"
build = pathlib.Path(sys.argv[1]) / "stm32-boot-st-contract"
subprocess.run(["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
                "-DCMAKE_BUILD_TYPE=Release", "-DABOX_BOOT_FW_VERSION=abox-boot-2.3.0-st",
                "-DCMAKE_TOOLCHAIN_FILE=" + str(source / "cmake/gcc-arm-none-eabi.cmake")], check=True)
subprocess.run(["cmake", "--build", str(build)], check=True)
verify = source / "tools/verify_boot_artifact.py"
subprocess.run([sys.executable, str(verify), str(build / "ABox_Boot.bin"),
                "--expected-version", "abox-boot-2.3.0-st"], check=True)
subprocess.run([sys.executable, str(verify), str(source / "dist/ABox_Boot.bin")], check=True)
