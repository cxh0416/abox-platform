from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    scanner = Path(__file__).resolve().parents[1] / "tools/check_firmware_serialization.py"
    with tempfile.TemporaryDirectory() as directory:
        product = Path(directory)
        fixtures = {
            "app/new_command.c": 'snprintf(s, n, "%llu", ts);',
            "shared/trace.c": 'printf("%+020lld", x);',
            "boot/log.c": 'printf("%llx", bits);',
            "platform_config/id.c": 'printf("%016" PRIx64, x);',
            "app/expiry.c": 'cJSON_AddNumberToObject(obj,\n "expiresAt", value);',
            "app/safe.c": '/* "%llu" */ printf("%%llu"); cJSON_AddRawToObject(obj, "expiresAt", s);',
            "app/tests/fake.c": 'printf("%llu", now);',
            "app/third_party/vendor.c": 'printf("%lld", x);',
        }
        for name, source in fixtures.items():
            path = product / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(source, encoding="utf-8")
        result = subprocess.run([sys.executable, scanner, "--product-root", product],
                                capture_output=True, text=True, check=False)
        expected = {"app/new_command.c", "shared/trace.c", "boot/log.c",
                    "platform_config/id.c", "app/expiry.c"}
        if result.returncode != 1 or len(result.stdout.splitlines()) != len(expected):
            raise AssertionError((result.returncode, result.stdout, result.stderr))
        for name in expected:
            if f"{product / name}:1:" not in result.stdout:
                raise AssertionError((name, result.stdout))


if __name__ == "__main__":
    main()
