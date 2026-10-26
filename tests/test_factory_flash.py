import hashlib
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import factory_flash_stm32f105 as factory


class Probe:
    def __init__(self, partial=False):
        self.memory = bytearray(b"\0" * (factory.END - factory.FLASH))
        self.events = []
        self.partial = partial

    def halt(self):
        self.events.append("halt")

    def memory_read16(self, address, size):
        return [256]

    def memory_read32(self, address, size):
        if address == factory.UID:
            return [1, 2, 3]
        if address == 0xE0042000:
            return [0x10016418]
        offset = address - factory.FLASH
        return list(struct.unpack(f"<{size}I", self.memory[offset:offset + 4 * size]))

    def erase(self):
        self.events.append("erase")
        self.memory[:] = b"\xff" * len(self.memory)
        if self.partial:
            self.memory[-1] = 0

    def flash(self, image, address):
        self.events.append("flash")
        self.memory[:len(image)] = image

    def reset(self, halt):
        self.events.append("reset")


class FactoryTest(unittest.TestCase):
    def test_full_erase_includes_old_config(self):
        probe = Probe()
        factory.factory_flash(probe, b"B" * 0x8000, b"A" * 20, "000000010000000200000003")
        self.assertEqual(probe.events, ["halt", "erase", "flash", "reset"])
        self.assertEqual(probe.memory[-0x1800:], b"\xff" * 0x1800)

    def test_unaligned_app_end_still_checks_remaining_flash(self):
        probe = Probe()
        factory.factory_flash(probe, b"B" * 0x8000, b"A" * 21, "000000010000000200000003")
        self.assertEqual(probe.events[-1], "reset")

    def test_wrong_device_never_erased(self):
        probe = Probe()
        with self.assertRaisesRegex(ValueError, "UID differs"):
            factory.factory_flash(probe, b"", b"", "000000000000000000000000")
        self.assertNotIn("erase", probe.events)

    def test_partial_erase_never_programmed(self):
        probe = Probe(partial=True)
        with self.assertRaisesRegex(RuntimeError, "blank verification failed"):
            factory.factory_flash(probe, b"", b"", "000000010000000200000003")
        self.assertNotIn("flash", probe.events)

    def test_reject_images_before_connect(self):
        boot = b"B" * 0x8000
        manifest = {"sha256": hashlib.sha256(boot).hexdigest()}
        app = struct.pack("<II", 0x20010000, factory.APP + 9) + b"bench\0"
        factory.validate_images(boot, app, manifest, "bench")
        for bad_boot, bad_app, version in (
            (b"X" * 0x8000, app, "bench"), (boot, app, "other"),
            (boot, app + b"\0" * (factory.STATE - factory.APP), "bench"),
            (boot, struct.pack("<II", 0x20010000, factory.STATE | 1) + b"bench\0", "bench"),
        ):
            with self.assertRaises(ValueError):
                factory.validate_images(bad_boot, bad_app, manifest, version)


if __name__ == "__main__":
    unittest.main()
