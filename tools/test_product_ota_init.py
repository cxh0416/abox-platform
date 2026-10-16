"""Run startup failure injection against a product's actual OTA adapter."""
from pathlib import Path
import argparse
import shutil
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--product-root", type=Path, required=True)
args = parser.parse_args()
product = args.product_root.resolve()
platform = product / "platform"
source = product / "app/Core/User/ota/src/ota_service.c"
build = product / "app/build/host-tests"
build.mkdir(parents=True, exist_ok=True)
exe = build / "product_ota_init_test.exe"
include_dirs = [platform / "tests/product_ota_stubs", product / "app/Core/User/ota/include"]
include_dirs += [platform / "components" / name / "include"
                 for name in ("boot_v2", "ec800", "mqtt", "ota", "core")]
command = [shutil.which("gcc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
           "-Wno-unused-parameter", "-DPRODUCT_OTA_SOURCE=\"" + source.as_posix() + "\"",
           *["-I" + str(p) for p in include_dirs],
           str(platform / "tests/product_ota_init_test.c"),
           str(platform / "components/ec800/src/abox_ec800_at.c"), "-o", str(exe)]
subprocess.run(command, check=True)
subprocess.run([str(exe)], check=True)
print("Product OTA startup failure injection: PASS (" + product.name + ")")
