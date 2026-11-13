"""Build the documented F105 entry and link the real CAN Port for both MCUs."""
import pathlib
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parents[1]
out = pathlib.Path(sys.argv[1])
toolchain = root / "cmake/toolchains/gcc-arm-none-eabi-f105.cmake"
for name, hardware, board, vendor_symbol in (
        ("gd", "gd32f105_ec800_v1", "v1", "can_init"),
        ("st", "stm32f105_ec800_v1", "v2", "HAL_CAN_Init")):
    build = out / ("f105-example-" + name)
    subprocess.run(["cmake", "-S", str(root / "examples/f105_app"), "-B", str(build),
                    "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
                    "-DCMAKE_TOOLCHAIN_FILE=" + str(toolchain),
                    "-DABOX_HARDWARE=" + hardware, "-DABOX_BOARD_VERSION=" + board,
                    "-DABOX_EXAMPLE_CAN=ON", "-DABOX_EXAMPLE_CAN_EVENTS=ON"], check=True)
    subprocess.run(["cmake", "--build", str(build)], check=True)
    symbols = subprocess.check_output(["arm-none-eabi-nm", str(build / "ABoxF105Example")], text=True)
    names = {line.split()[-1] for line in symbols.splitlines() if line.split()}
    assert {"ABoxBoard_Init", "ABoxCanPort_Init", "ABoxCanPort_DispatchEventIrq", vendor_symbol} <= names, name
    if name == "st":
        assert "MX_CAN1_Init" not in names and "MX_CAN2_Init" not in names
print("F105 examples linked both board implementations and public CAN ports")
