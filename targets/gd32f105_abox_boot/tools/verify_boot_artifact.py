#!/usr/bin/env python3
"""Validate the GD32 Boot image and its advertised descriptor."""
import argparse
import hashlib
import struct
import zlib
from pathlib import Path


def verify(path: Path) -> dict[str, str | int]:
    image = path.read_bytes()
    if len(image) != 0x8000:
        raise ValueError("GD Boot image must be exactly 32 KiB")
    sp, pc = struct.unpack_from("<II", image)
    if sp != 0x20010000 or not pc & 1 or not 0x08000000 <= (pc & ~1) < 0x08007F00:
        raise ValueError("invalid GD Boot vector")
    if image[0x7000:0x7800] != bytes(0x800):
        raise ValueError("reserved GD Boot test page is not padding")
    desc = image[0x7F00:]
    fields = struct.unpack_from("<IHHIHHH", desc)
    if fields != (0x32564241, 1, 256, 15, 2, 3, 0):
        raise ValueError("GD Boot descriptor fields mismatch")
    if desc[20:52].split(b"\0", 1)[0] != b"abox_gd32f105" or \
       desc[52:116].split(b"\0", 1)[0] != b"abox-boot-2.3.0-gd":
        raise ValueError("GD Boot descriptor identity mismatch")
    if zlib.crc32(desc[:252]) != struct.unpack_from("<I", desc, 252)[0]:
        raise ValueError("GD Boot descriptor CRC mismatch")
    for marker in (b"ABOX_GD_BOOT_RAM_SMOKE", b"ABOX_GD_BOOT_INSTALL_PROBE", b"testBuild=1"):
        if marker in image:
            raise ValueError("GD Boot contains a test marker")
    return {"size": len(image), "sha256": hashlib.sha256(image).hexdigest(),
            "crc32": f"{zlib.crc32(image):08X}", "boot_version": "abox-boot-2.3.0-gd"}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    args = parser.parse_args()
    for key, value in verify(args.image).items():
        print(f"{key}={value}")


if __name__ == "__main__":
    main()
