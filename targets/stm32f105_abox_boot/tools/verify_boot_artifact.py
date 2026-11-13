#!/usr/bin/env python3
import argparse
import hashlib
import struct
import zlib
from pathlib import Path


BOOT_BASE = 0x08000000
BOOT_SIZE = 0x8000
DESCRIPTOR_OFFSET = 0x7F00
DESCRIPTOR_SIZE = 0x100
RAM_START = 0x20000000
RAM_END = 0x20010000
EXPECTED_PRODUCT = "abox_stm32f105"
EXPECTED_VERSION = "abox-boot-2.3.0"


def c_string(raw: bytes) -> str:
    return raw.split(b"\0", 1)[0].decode("ascii")


def verify(path: Path, expected_version: str = EXPECTED_VERSION) -> dict[str, object]:
    image = path.read_bytes()
    if len(image) != BOOT_SIZE:
        raise ValueError(f"Boot image must be exactly 0x{BOOT_SIZE:X} bytes, got 0x{len(image):X}")
    for forbidden in (b"AT+QHTTP", b"https://", b"top_flying_wing", b"top-flying-wing"):
        if forbidden in image:
            raise ValueError(f"product/download marker leaked into frozen Boot: {forbidden!r}")

    initial_sp, reset_handler = struct.unpack_from("<II", image, 0)
    if not RAM_START <= initial_sp <= RAM_END or initial_sp % 4:
        raise ValueError(f"invalid initial SP 0x{initial_sp:08X}")
    if not (BOOT_BASE <= (reset_handler & ~1) < BOOT_BASE + BOOT_SIZE) or not (reset_handler & 1):
        raise ValueError(f"invalid reset handler 0x{reset_handler:08X}")

    raw = image[DESCRIPTOR_OFFSET : DESCRIPTOR_OFFSET + DESCRIPTOR_SIZE]
    magic, abi, length, features, major, minor, patch, _ = struct.unpack_from("<IHHIHHHH", raw)
    product = c_string(raw[20:52])
    version = c_string(raw[52:116])
    stored_crc = struct.unpack_from("<I", raw, 252)[0]
    actual_crc = zlib.crc32(raw[:252]) & 0xFFFFFFFF
    expected = (0x32564241, 1, DESCRIPTOR_SIZE, 0xF, 2, 3, 0)
    actual = (magic, abi, length, features, major, minor, patch)
    if actual != expected:
        raise ValueError(f"descriptor fields mismatch: {actual!r}")
    if product != EXPECTED_PRODUCT or version != expected_version:
        raise ValueError(f"descriptor identity mismatch: {product!r}, {version!r}")
    if stored_crc != actual_crc:
        raise ValueError(f"descriptor CRC mismatch: stored=0x{stored_crc:08X} actual=0x{actual_crc:08X}")

    return {
        "size": len(image),
        "sha256": hashlib.sha256(image).hexdigest(),
        "crc32": f"{zlib.crc32(image) & 0xFFFFFFFF:08X}",
        "initial_sp": f"0x{initial_sp:08X}",
        "reset_handler": f"0x{reset_handler:08X}",
        "descriptor_crc32": f"0x{stored_crc:08X}",
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    parser.add_argument("--expected-version", choices=(EXPECTED_VERSION, EXPECTED_VERSION + "-st"),
                        default=EXPECTED_VERSION)
    args = parser.parse_args()
    result = verify(args.image, args.expected_version)
    print("ABox Boot artifact verified")
    for key, value in result.items():
        print(f"{key}={value}")


if __name__ == "__main__":
    main()
