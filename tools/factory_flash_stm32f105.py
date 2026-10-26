"""Factory-test entry: erase ALL STM32F105RC Flash, verify blank, then program.

This deliberately has no preserve-config mode. Modem UFS and server enrollment
records are outside MCU Flash and are not erased by this tool.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

FLASH = 0x08000000
APP = 0x08008000
STATE = 0x0803E800
END = 0x08040000
UID = 0x1FFFF7E8
CHUNK = 1024


def validate_images(boot, app, boot_manifest, version):
    if len(boot) != APP - FLASH or hashlib.sha256(boot).hexdigest() != boot_manifest["sha256"]:
        raise ValueError("frozen Boot size/hash differs from its manifest")
    if not 8 <= len(app) <= STATE - APP:
        raise ValueError("App overlaps Boot State/config or has no vectors")
    sp, reset = struct.unpack_from("<II", app)
    if not 0x20000000 < sp <= 0x20010000 or sp % 4:
        raise ValueError("invalid App stack vector")
    if not reset & 1 or not APP <= reset & ~1 < APP + len(app):
        raise ValueError("invalid App reset vector")
    if not version or version.encode("ascii") + b"\0" not in app:
        raise ValueError("expected App version marker is missing")


def read_bytes(link, address, size):
    aligned = address & ~3
    prefix = address - aligned
    count = (prefix + size + 3) // 4
    words = link.memory_read32(aligned, count)
    if len(words) != count:
        raise RuntimeError(f"short Flash read at 0x{address:08X}")
    return struct.pack(f"<{len(words)}I", *words)[prefix:prefix + size]


def check_range(link, start, end, image=None):
    for address in range(start, end, CHUNK):
        size = min(CHUNK, end - address)
        expected = b"\xff" * size if image is None else image[address-start:address-start+size]
        if read_bytes(link, address, size) != expected:
            raise RuntimeError(f"{'blank' if image is None else 'program'} verification failed at 0x{address:08X}")


def factory_flash(link, boot, app, expected_uid):
    link.halt()
    actual_uid = "".join(f"{word:08X}" for word in link.memory_read32(UID, 3))
    if actual_uid != expected_uid.upper():
        raise ValueError(f"target UID differs: {actual_uid}")
    if link.memory_read16(0x1FFFF7E0, 1) != [256] or link.memory_read32(0xE0042000, 1)[0] & 0xFFF != 0x418:
        raise ValueError("target must be STM32F105RC with 256 KiB Flash")
    print(f"Target UID={actual_uid}; erasing all 256 KiB MCU Flash", flush=True)
    link.erase()  # SEGGER JLINK_EraseChip, not page-selective programming erase.
    check_range(link, FLASH, END)
    print("PASS entire MCU Flash is FF, including Boot State A/B and config", flush=True)
    image = boot + app
    link.flash(image, FLASH)
    check_range(link, FLASH, FLASH + len(image), image)
    check_range(link, FLASH + len(image), END)
    print("PASS Boot/App readback; all remaining Flash still FF before first boot", flush=True)
    link.reset(halt=False)


def release_debug(link):
    # Remove temporary reset catches/watchdog freezes; preserve DWT clock tracing.
    for address, mask in ((0xE0042004, 0x307), (0xE000EDFC, 0x401)):
        value = link.memory_read32(address, 1)[0]
        link.memory_write32(address, [value & ~mask])
    link.restart()
    link.memory_write32(0xE000EDF0, [0xA05F0000])


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", type=int, required=True)
    parser.add_argument("--uid", required=True, help="three UID words as 24 hexadecimal digits")
    parser.add_argument("--dll", type=Path, required=True)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--version", required=True)
    args = parser.parse_args(argv)
    frozen = Path(__file__).resolve().parents[1] / "targets/stm32f105_abox_boot"
    boot_path = frozen / "dist/ABox_Boot.bin"
    sys.path.insert(0, str(frozen / "tools"))
    from verify_boot_artifact import verify
    verify(boot_path)
    boot, app = boot_path.read_bytes(), args.app.read_bytes()
    validate_images(boot, app, json.loads((frozen / "dist/manifest.json").read_text(encoding="utf-8-sig")), args.version)
    if len(args.uid) != 24 or any(c not in "0123456789abcdefABCDEF" for c in args.uid):
        raise ValueError("--uid must be 24 hexadecimal digits")
    print(f"App sha256={hashlib.sha256(app).hexdigest()}", flush=True)
    import pylink
    link = pylink.JLink(lib=pylink.library.Library(dllpath=str(args.dll)))
    complete = False
    try:
        link.exec_command("DisableAutoUpdateFW")
        link.open(serial_no=args.serial)
        for command in ("SetRestartOnClose=0", "SetSkipDebugDeInit=1", "ForceAttachTarget 1"):
            link.exec_command(command)
        link.set_tif(pylink.enums.JLinkInterfaces.SWD)
        link.connect("STM32F105RC", speed=1000)
        factory_flash(link, boot, app, args.uid)
        complete = True
    finally:
        try:
            if complete:
                release_debug(link)
                if link.halted():
                    raise RuntimeError("MCU did not resume after factory programming")
        finally:
            link.close()
    print("PASS factory programming complete; MCU running from Boot", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
