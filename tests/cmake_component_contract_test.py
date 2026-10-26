"""Exercise configure-time component and hardware gates with synthetic users."""

import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def configure(name: str, arguments: str, success: bool, diagnostic: str = "",
              inspect_build: bool = False, extra: str = "",
              attach_function: str = "abox_platform_attach_components") -> None:
    with tempfile.TemporaryDirectory(prefix=f"cmake-{name}-", dir=ROOT / "build") as folder:
        source = Path(folder)
        config = source / "config"
        config.mkdir()
        (config / "abox_product_config.h").write_text(
            "/* synthetic hardware contract */\nABOX_SCHEDULER_BAREMETAL\n", encoding="utf-8")
        (source / "consumer.c").write_text("int main(void) { return 0; }\n", encoding="utf-8")
        (source / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.22)\n"
            "project(component_contract C)\n"
            f"add_subdirectory(\"{ROOT.as_posix()}\" platform-build)\n"
            "add_executable(consumer consumer.c)\n"
            f"set(ABOX_PRODUCT_CONFIG_DIR \"{config.as_posix()}\")\n"
            f"{attach_function}(consumer {arguments})\n"
            f"{extra}\n",
            encoding="utf-8")
        result = subprocess.run(["cmake", "-S", str(source), "-B", str(source / "build"),
                                 "-G", "Ninja", "-DBUILD_TESTING=OFF"],
                                capture_output=True, text=True)
        output = result.stdout + result.stderr
        assert (result.returncode == 0) == success, f"{name}: {output}"
        if diagnostic:
            assert diagnostic in output, f"{name}: {output}"
        if inspect_build:
            built = subprocess.run(["cmake", "--build", str(source / "build")],
                                   capture_output=True, text=True)
            assert built.returncode == 0, f"{name} build: {built.stdout}{built.stderr}"
            assert (source / "build/platform-build/components/mqtt/libabox_mqtt_v4.a").exists()
            assert not (source / "build/platform-build/components/legacy/ota/libabox_ota.a").exists()
            assert not (source / "build/platform-build/components/mqtt/libabox_mqtt_ec800_rx.a").exists()


def main() -> None:
    (ROOT / "build").mkdir(exist_ok=True)
    configure("generic-mqtt", "SCHEDULER BAREMETAL COMPONENTS core mqtt_v4", True,
              inspect_build=True)
    configure("v4-components", "SCHEDULER BAREMETAL COMPONENTS core cjson mqtt_v4 mqtt_digest "
              "mqtt_request mqtt_idempotency mqtt_registry ec800 ec800_ufs ec800_ufs_at boot_v2_common "
              "ufs_journal mqtt_idem_ufs cloud_service cloud_v4", True,
              extra="target_link_libraries(consumer abox::cloud_v4 abox::mqtt_idem_ufs abox::mqtt_registry)")
    registry_path = (ROOT / "tests/fixtures/v4/registry-contract.json").as_posix()
    configure("registry-testing-disabled", "SCHEDULER BAREMETAL COMPONENTS core mqtt_v4", False,
              "BUILD_TESTING must be enabled",
              extra=f'abox_platform_verify_registry(consumer REGISTRY "{registry_path}")')
    configure("registry-missing-target", "SCHEDULER BAREMETAL COMPONENTS core mqtt_v4", False,
              "host target and REGISTRY are required",
              extra=f'abox_platform_verify_registry(missing REGISTRY "{registry_path}")')
    configure("hardware", "SCHEDULER BAREMETAL HARDWARE stm32f105_ec800_v1 COMPONENTS core", True)
    configure("standard-port", "SCHEDULER BAREMETAL HARDWARE stm32f105_ec800_v1 COMPONENTS core ec_power", True,
              extra="abox_platform_attach_standard_port(consumer HARDWARE stm32f105_ec800_v1)")
    configure("standard-app", "PRODUCT_CONFIG_DIR ${ABOX_PRODUCT_CONFIG_DIR} SCHEDULER BAREMETAL", True,
              attach_function="abox_platform_attach_standard_app")
    configure("standard-app-missing-scheduler", "PRODUCT_CONFIG_DIR ${ABOX_PRODUCT_CONFIG_DIR}", False,
              "PRODUCT_CONFIG_DIR and SCHEDULER are",
              attach_function="abox_platform_attach_standard_app")
    configure("standard-port-unknown", "SCHEDULER BAREMETAL COMPONENTS core", False,
              "HARDWARE must be stm32f105_ec800_v1",
              extra="abox_platform_attach_standard_port(consumer HARDWARE unknown)")
    configure("hardware-mismatch", "SCHEDULER FREERTOS HARDWARE stm32f105_ec800_v1 COMPONENTS core",
              False, "scheduler disagrees")
    configure("scheduler", "SCHEDULER UNKNOWN COMPONENTS core", False, "SCHEDULER")
    configure("hardware-unknown", "SCHEDULER BAREMETAL HARDWARE unknown COMPONENTS core", False, "HARDWARE")
    configure("missing-dependency", "SCHEDULER BAREMETAL COMPONENTS mqtt_runtime", False, "requires")
    configure("legacy-ota", "SCHEDULER BAREMETAL COMPONENTS ota", False, "legacy Boot/OTA")


if __name__ == "__main__":
    main()
