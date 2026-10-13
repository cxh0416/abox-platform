#!/usr/bin/env python3
"""STM32F105 / Cortex-M3 fallback debugger using raw SEGGER CoreSight AP/DP.

Uses only Python's standard library and the installed J-Link shared library.
No automatic reset, reconnect loop, Flash programming, or capture files.
"""
import argparse
import ctypes
import json
import os
import signal
import struct
import sys
import time
from pathlib import Path


DHCSR, DCRSR, DCRDR = 0xE000EDF0, 0xE000EDF4, 0xE000EDF8
DEMCR, AIRCR, DBGMCU_CR = 0xE000EDFC, 0xE000ED0C, 0xE0042004
FP_CTRL, FP_COMP = 0xE0002000, 0xE0002008
UID = 0x1FFFF7E8
S_HALT, S_REGRDY, S_RESET_ST = 1 << 17, 1 << 16, 1 << 25
KEY = 0xA05F0000
REGISTER_NAMES = ["r%d" % i for i in range(13)] + ["sp", "lr", "pc", "xpsr", "msp", "psp"]


class DebugError(RuntimeError):
    pass


class Segger:
    def __init__(self, path):
        self.lib = ctypes.CDLL(str(path))
        signatures = {
            "Open": ([], ctypes.c_char_p),
            "Close": ([], None),
            "ExecCommand": ([ctypes.c_char_p, ctypes.c_void_p, ctypes.c_int], ctypes.c_int),
            "TIF_Select": ([ctypes.c_int], ctypes.c_int),
            "SetSpeed": ([ctypes.c_uint32], None),
            "Connect": ([], ctypes.c_int),
            "CORESIGHT_ReadAPDPReg": ([ctypes.c_uint8, ctypes.c_uint8,
                                      ctypes.POINTER(ctypes.c_uint32)], ctypes.c_int),
            "CORESIGHT_WriteAPDPReg": ([ctypes.c_uint8, ctypes.c_uint8,
                                       ctypes.c_uint32], ctypes.c_int),
        }
        for name, (args, result) in signatures.items():
            function = getattr(self.lib, "JLINKARM_" + name)
            function.argtypes, function.restype = args, result
        self.opened = False

    def command(self, value):
        error = ctypes.create_string_buffer(1024)
        result = self.lib.JLINKARM_ExecCommand(value.encode("ascii"), error, len(error))
        if result < 0 or error.value:
            raise DebugError("%s: %s (rc=%d)" % (value, error.value.decode(errors="replace"), result))

    def connect(self, speed):
        self.command("DisableAutoUpdateFW")
        error = self.lib.JLINKARM_Open()
        if error:
            raise DebugError("J-Link open: " + error.decode(errors="replace"))
        self.opened = True
        for value in ("Device=Cortex-M3", "ForceAttachTarget=1", "InhibitConnectRetries=1", "SetRestartOnClose=0",
                      "SetSkipDebugDeInit=1", "DisableFlashBPs"):
            self.command(value)
        if self.lib.JLINKARM_TIF_Select(1) < 0:
            raise DebugError("Cannot select SWD")
        self.lib.JLINKARM_SetSpeed(speed)
        if self.lib.JLINKARM_Connect() < 0:
            raise DebugError("J-Link connect failed; no automatic retry")

    def read_dap(self, index, ap=1):
        value = ctypes.c_uint32()
        result = self.lib.JLINKARM_CORESIGHT_ReadAPDPReg(index, ap, ctypes.byref(value))
        if result < 0:
            raise DebugError("DAP read index=%d APnDP=%d rc=%d" % (index, ap, result))
        return value.value

    def write_dap(self, index, value, ap=1):
        result = self.lib.JLINKARM_CORESIGHT_WriteAPDPReg(index, ap, value)
        if result < 0:
            raise DebugError("DAP write index=%d APnDP=%d rc=%d" % (index, ap, result))

    def close(self):
        if self.opened:
            self.lib.JLINKARM_Close()
            self.opened = False


class Swd:
    def __init__(self, backend, timeout=1.0):
        self.backend = backend
        self.timeout = timeout
        self.restore = []

    def read(self, address, size):
        if address < 0 or size <= 0 or size > 1024 * 1024 or address + size > 0x100000000:
            raise DebugError("Read requires a valid 32-bit range of 1..1048576 bytes")
        start, end = address & ~3, (address + size + 3) & ~3
        self.backend.write_dap(2, 0, 0)
        self.backend.write_dap(0, 0x23000052)
        data = bytearray()
        for current in range(start, end, 4):
            if current == start or current % 1024 == 0:
                self.backend.write_dap(1, current)
            data.extend(struct.pack("<I", self.backend.read_dap(3)))
        return bytes(data[address - start:address - start + size])

    def u32(self, address):
        return struct.unpack("<I", self.read(address, 4))[0]

    def write32(self, address, value):
        if address % 4 or not 0 <= address <= 0xFFFFFFFC or not 0 <= value <= 0xFFFFFFFF:
            raise DebugError("Write requires an aligned 32-bit address and value")
        self.backend.write_dap(2, 0, 0)
        self.backend.write_dap(0, 0x23000002)
        self.backend.write_dap(1, address)
        self.backend.write_dap(3, value)

    def wait(self, predicate, label, timeout=None):
        deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
        while not predicate():
            if time.monotonic() >= deadline:
                raise DebugError(label + " timed out; no automatic reset or reconnect")
            time.sleep(0.01)

    def uid(self):
        return "".join("%08X" % value for value in struct.unpack("<III", self.read(UID, 12)))

    def status(self):
        return {"uid": self.uid(), "halted": bool(self.u32(DHCSR) & S_HALT),
                "dhcsr": self.u32(DHCSR), "vtor": self.u32(0xE000ED08),
                "app_vector": self.u32(0x08008004), "cfsr": self.u32(0xE000ED28),
                "hfsr": self.u32(0xE000ED2C)}

    def prepare(self):
        # Snapshot everything before the first core-state change. Restore halt
        # state as observed on attachment; do not resume a preexisting halt.
        dhcsr = self.u32(DHCSR)
        demcr, debug_cr = self.u32(DEMCR), self.u32(DBGMCU_CR)
        control = (dhcsr & 9) | (2 if dhcsr & S_HALT else 0)
        self.restore = [(DEMCR, demcr), (DBGMCU_CR, debug_cr), (DHCSR, KEY | control)]
        self.write32(DBGMCU_CR, debug_cr | 0x307)
        return dhcsr, demcr

    def halt(self):
        self.write32(DHCSR, KEY | 3)
        self.wait(lambda: self.u32(DHCSR) & S_HALT, "Core halt")

    def register(self, index):
        if not self.u32(DHCSR) & S_HALT:
            raise DebugError("CPU registers require a halted core")
        self.write32(DCRSR, index)
        self.wait(lambda: self.u32(DHCSR) & S_REGRDY, "Register transfer")
        return self.u32(DCRDR)

    def snapshot(self):
        registers = {name: self.register(index) for index, name in enumerate(REGISTER_NAMES)}
        registers["special"] = self.register(20)
        result = {"registers": registers, "fault": {
            name: self.u32(address) for name, address in
            (("cfsr", 0xE000ED28), ("hfsr", 0xE000ED2C), ("dfsr", 0xE000ED30),
             ("mmfar", 0xE000ED34), ("bfar", 0xE000ED38), ("vtor", 0xE000ED08))}}
        lr = registers["lr"]
        if lr in (0xFFFFFFF1, 0xFFFFFFF9, 0xFFFFFFFD):
            stack = registers["psp"] if lr & 4 else registers["msp"]
            if stack % 4 == 0 and 0x20000000 <= stack <= 0x20010000 - 32:
                words = struct.unpack("<8I", self.read(stack, 32))
                result["exception_frame"] = dict(zip(
                    ("r0", "r1", "r2", "r3", "r12", "lr", "pc", "xpsr"), words))
                result["exception_stack"] = stack
        return result

    def breakpoint(self, address):
        if not 0 <= address < 0x20000000:
            raise DebugError("FPB v1 instruction breakpoint requires a code address below 0x20000000")
        address &= ~1  # Accept Thumb symbols; caller still checks instruction start.
        control = self.u32(FP_CTRL)
        if control >> 28:
            raise DebugError("Only Cortex-M3 FPB v1 is supported")
        count = ((control >> 4) & 15) | ((control >> 8) & 0x70)
        for index in range(count):
            slot = FP_COMP + 4 * index
            previous = self.u32(slot)
            if not previous & 1:
                self.restore[:0] = [(slot, previous), (FP_CTRL, control | 2)]
                half = 0x80000000 if address & 2 else 0x40000000
                self.write32(slot, half | (address & 0x1FFFFFFC) | 1)
                self.write32(FP_CTRL, 3)
                return
        raise DebugError("No free FPB instruction comparator")

    def capture(self, timeout, reset=False, breakpoint=None):
        dhcsr, demcr = self.prepare()
        if dhcsr & S_HALT:
            raise DebugError("Core was already halted; use regs/fault instead of capture")
        self.halt()
        if breakpoint is not None:
            self.breakpoint(breakpoint)
        self.write32(DEMCR, demcr | 0x400)
        if reset:
            # Catch reset once, then release it while retaining HardFault catch.
            self.write32(DEMCR, demcr | 0x401)
            self.u32(DHCSR)  # Clear sticky reset status before this reset.
            self.write32(AIRCR, 0x05FA0004)
            self.wait(lambda: self.u32(DHCSR) & S_RESET_ST, "Reset acknowledgement")
            self.wait(lambda: self.u32(DHCSR) & S_HALT, "Reset catch")
            self.write32(DEMCR, (demcr | 0x400) & ~1)
        self.write32(DHCSR, KEY | 1)
        emit({"stage": "armed", "reset": reset, "timeout_seconds": timeout})
        self.wait(lambda: self.u32(DHCSR) & S_HALT, "Fault/breakpoint capture", timeout)
        return self.snapshot()

    def finish(self):
        errors = []
        pending, self.restore = self.restore, []
        for address, value in pending:
            try:
                self.write32(address, value)
            except (DebugError, OSError) as error:
                errors.append(str(error))
        if errors:
            raise DebugError("State restoration failed: " + "; ".join(errors) +
                             "; use cleanup once access is restored")

    def cleanup(self):
        errors = []
        def attempt(operation):
            try:
                operation()
            except (DebugError, OSError) as error:
                errors.append(str(error))
        try:
            control = self.u32(FP_CTRL)
            if control >> 28:
                raise DebugError("Only Cortex-M3 FPB v1 is supported")
            count = ((control >> 4) & 15) | ((control >> 8) & 0x70)
            for index in range(count):
                attempt(lambda slot=FP_COMP + 4 * index: self.write32(slot, 0))
            attempt(lambda: self.write32(FP_CTRL, 2))
            attempt(lambda: self.write32(DEMCR, self.u32(DEMCR) & ~0x401))
            attempt(lambda: self.write32(DBGMCU_CR, self.u32(DBGMCU_CR) & ~0x307))
        except (DebugError, OSError) as error:
            errors.append(str(error))
        finally:
            attempt(lambda: self.write32(DHCSR, KEY | 1))
            attempt(lambda: self.wait(lambda: not self.u32(DHCSR) & S_HALT, "Core resume"))
            attempt(lambda: self.write32(DHCSR, KEY))
        if errors:
            raise DebugError("Cleanup incomplete: " + "; ".join(errors))


def number(value):
    return int(value, 0)


def positive(value):
    result = float(value)
    if not 0 < result <= 600:
        raise argparse.ArgumentTypeError("timeout must be >0 and <=600 seconds")
    return result


def uid_argument(value):
    value = value.upper()
    if len(value) != 24 or any(ch not in "0123456789ABCDEF" for ch in value):
        raise argparse.ArgumentTypeError("UID must be 24 hex characters in STM32 word order")
    return value


def emit(value):
    print(json.dumps(value, indent=2), flush=True)


def parser():
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--library", default="/opt/SEGGER/JLink/libjlinkarm.so")
    result.add_argument("--speed", type=int, default=1000, help="SWD kHz (default: 1000)")
    result.add_argument("--expect-uid", type=uid_argument)
    result.add_argument("--io-timeout", type=positive, default=1.0)
    commands = result.add_subparsers(dest="action", required=True)
    commands.add_parser("status", help="Read UID and core/fault status without halting")
    read = commands.add_parser("read", help="Read memory via AHB-AP; print hex by default")
    read.add_argument("address", type=number)
    read.add_argument("size", type=number)
    read.add_argument("--output", type=Path, help="Explicit binary output; refuses overwrite")
    commands.add_parser("regs", help="Briefly halt, read registers, restore entry state")
    commands.add_parser("fault", help="Read registers, SCB and valid exception stack frame")
    capture = commands.add_parser("capture", help="Catch HardFault or one FPB instruction breakpoint")
    capture.add_argument("--timeout", type=positive, default=10.0)
    capture.add_argument("--reset", action="store_true", help="Explicitly request ONE software reset")
    capture.add_argument("--breakpoint", type=number, help="Address at an instruction start")
    commands.add_parser("cleanup", help="Clear FPB/catches/watchdog freeze and resume/detach")
    return result


def run(args, backend):
    swd = Swd(backend, args.io_timeout)
    try:
        backend.connect(args.speed)
        actual_uid = swd.uid()
        if args.expect_uid and actual_uid != args.expect_uid:
            raise DebugError("UID mismatch: expected %s, got %s" % (args.expect_uid, actual_uid))
        if args.action == "status":
            emit(swd.status())
        elif args.action == "read":
            data = swd.read(args.address, args.size)
            if args.output:
                fd = os.open(args.output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
                with os.fdopen(fd, "wb") as stream:
                    stream.write(data)
                emit({"address": args.address, "size": len(data), "output": str(args.output)})
            else:
                emit({"address": args.address, "size": len(data), "hex": data.hex()})
        elif args.action in ("regs", "fault"):
            swd.prepare()
            swd.halt()
            result = swd.snapshot()
            emit(result["registers"] if args.action == "regs" else result)
        elif args.action == "capture":
            emit(swd.capture(args.timeout, args.reset, args.breakpoint))
        elif args.action == "cleanup":
            swd.cleanup()
            emit({"stage": "cleaned", "uid": actual_uid, "halted": bool(swd.u32(DHCSR) & S_HALT)})
    finally:
        try:
            swd.finish()
        finally:
            backend.close()


def main(argv=None):
    command = parser()
    args = command.parse_args(argv)
    if not 1 <= args.speed <= 50000:
        command.error("speed must be 1..50000 kHz")
    if args.action not in ("status", "read") and not args.expect_uid:
        command.error("--expect-uid is required for core-state operations; obtain it with status")
    try:
        run(args, Segger(args.library))
        return 0
    except KeyboardInterrupt:
        print("Interrupted; attempted to restore entry state", file=sys.stderr)
        return 130
    except (DebugError, OSError, AttributeError) as error:
        print("SWD error: " + str(error), file=sys.stderr)
        return 2


def interrupt(*_):
    raise KeyboardInterrupt()


if __name__ == "__main__":
    signal.signal(signal.SIGTERM, interrupt)
    sys.exit(main())
