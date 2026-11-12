"""Exercise per-product Flash boundaries and reserved pages."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools/validate_flash_layout.ps1"
POWERSHELL = shutil.which("pwsh") or shutil.which("powershell")


@unittest.skipUnless(POWERSHELL, "PowerShell is required")
class FlashLayoutTest(unittest.TestCase):
    def run_layout(self, app_size, *options):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            boot = root / "boot.ld"
            app = root / "app.ld"
            boot.write_text("FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 0x8000\n", encoding="utf-8")
            app.write_text(f"FLASH (rx) : ORIGIN = 0x08008000, LENGTH = 0x{app_size:X}\n", encoding="utf-8")
            return subprocess.run(
                [POWERSHELL, "-NoProfile", "-File", str(SCRIPT), "-BootLinker", str(boot),
                 "-AppLinker", str(app), *options], capture_output=True, text=True,
                encoding="utf-8", errors="replace"
            )

    def test_product_boundaries_and_protected_pages(self):
        self.assertEqual(self.run_layout(0x36000).returncode, 0)
        self.assertNotEqual(self.run_layout(0x36800).returncode, 0)
        full = self.run_layout(0x36800, "-NoConfigRecovery")
        self.assertEqual(full.returncode, 0, full.stdout + full.stderr)
        self.assertNotIn("PASS config recovery", full.stdout)
        short = self.run_layout(0x36000, "-NoConfigRecovery", "-ExpectedAppEnd", "134471680")
        self.assertEqual(short.returncode, 0, short.stdout + short.stderr)
        self.assertNotIn("PASS config recovery", short.stdout)
        self.assertNotEqual(self.run_layout(0x37000, "-NoConfigRecovery", "-ExpectedAppEnd", "134475776").returncode, 0)


if __name__ == "__main__":
    unittest.main()
