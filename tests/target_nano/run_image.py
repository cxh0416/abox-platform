"""Execute an F105 SRAM smoke image using its linked ARM code and newlib."""

import argparse
from pathlib import Path
import struct

from unicorn import Uc, UC_ARCH_ARM, UC_HOOK_INTR, UC_MODE_MCLASS, UC_MODE_THUMB
from unicorn.arm_const import UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_SP, UC_CPU_ARM_CORTEX_M3


def run(path):
    image = path.read_bytes()
    stack, reset = struct.unpack_from("<II", image)
    ram = 0x20000000
    cpu = Uc(UC_ARCH_ARM, UC_MODE_MCLASS | UC_MODE_THUMB)
    cpu.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M3)
    cpu.mem_map(ram, 64 * 1024)
    cpu.mem_write(ram, image)
    cpu.reg_write(UC_ARM_REG_SP, stack)
    status = None

    def breakpoint(cpu, interrupt, _context):
        nonlocal status
        pc = cpu.reg_read(UC_ARM_REG_PC)
        if interrupt == 7 and cpu.mem_read(pc, 2) == b"\x00\xbe":  # BKPT #0
            status = cpu.reg_read(UC_ARM_REG_R0)
        cpu.emu_stop()

    cpu.hook_add(UC_HOOK_INTR, breakpoint)
    cpu.emu_start(reset, 0, count=5_000_000)
    if status is None:
        raise SystemExit(f"{path.name}: image did not reach its completion breakpoint")
    if status:
        raise SystemExit(f"{path.name}: target check failed at source line {status}")
    print(f"{path.name}: PASS (Cortex-M3)")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    run(parser.parse_args().image)
