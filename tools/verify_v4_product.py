"""Check compiled product packets and enrollment identity against service code.

No device, broker, database initialization or production configuration is used.
Product host tests implement --describe using their production adapters.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys


def require(condition, message):
    if not condition:
        raise ValueError(message)


def verify(product, wire, identity, products, contracts, adapter_for, topic_matches):
    require(product in products, f"service product is missing: {product}")
    contract = contracts.get(identity.get("hardwareContract"))
    require(contract is not None and contract.product == product,
            "firmware enrollment hardware contract is not registered for this product")
    require(int(contract.mqtt.tls_profile_id) == identity.get("tlsProfileId"),
            "firmware/service MQTT TLS profile differs")
    require(not identity.get("requirePsk") or contract.mqtt.psk_enrollment,
            "firmware requests PSK but the registered contract does not support it")
    adapter = adapter_for(product, products[product])
    expected = {"manifest": ("manifest", 1, 1), "heartbeat": ("heartbeat", 0, 0),
                "report": ("status_report", 1, 0)}
    seen, device_ids, firmware = set(), set(), set()
    for packet in wire.get("packets", []):
        topic, payload = packet["topic"], packet["payload"]
        suffix = topic.rsplit("/", 1)[-1]
        require(suffix in expected, f"unexpected bootstrap topic: {topic}")
        kind, qos, retain = expected[suffix]
        require(payload.get("version") == "4.0", "product bootstrap is not MQTT V4")
        require((packet["qos"], packet["retain"]) == (qos, retain),
                f"incorrect bootstrap QoS/retain: {topic}")
        subscriptions = [level for pattern, level in adapter.subscription_specs
                         if topic_matches(pattern, topic)]
        require(subscriptions and max(subscriptions) >= qos,
                f"service does not subscribe with the required QoS: {topic}")
        event = adapter.parse(topic, json.dumps(payload, separators=(",", ":")).encode())
        require(event is not None and event.kind == kind,
                f"service adapter rejects compiled {suffix} packet")
        seen.add(kind)
        device_ids.add(event.vid)
        if kind in {"manifest", "heartbeat"}:
            firmware.add(event.fw_version)
    require(seen == {"manifest", "heartbeat", "status_report"},
            "compiled bootstrap must contain Manifest, heartbeat and complete state")
    require(len(device_ids) == 1 and all(device_ids) and len(firmware) == 1 and all(firmware),
            "compiled bootstrap identity or firmware differs between packets")
    return products[product]["protocol"]


def describe(executable):
    completed = subprocess.run([str(executable.resolve()), "--describe"], check=True,
                               capture_output=True, text=True, encoding="utf-8", timeout=30)
    return json.loads(completed.stdout)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--product", required=True)
    parser.add_argument("--service-root", type=Path, required=True)
    parser.add_argument("--packet-test", type=Path, required=True)
    parser.add_argument("--identity-test", type=Path, required=True)
    parser.add_argument("--lifecycle-test", type=Path, required=True,
                        help="host test linking the production product adapter and shared AppService")
    args = parser.parse_args(argv)
    try:
        service = args.service_root.resolve()
        require((service / "device_ota/adapters.py").is_file(), "service source is missing")
        sys.path.insert(0, str(service))
        from device_ota.abox_hardware_contracts import HARDWARE_CONTRACTS
        from device_ota.adapters import adapter_for
        from device_ota.products import load_products
        from paho.mqtt.client import topic_matches_sub
        # Explicit source defaults: never load a production products.json.
        products = load_products(service / "deploy/ota-products.json.example")
        subprocess.run([str(args.lifecycle_test.resolve())], check=True, timeout=30)
        protocol = verify(args.product, describe(args.packet_test), describe(args.identity_test),
                          products, HARDWARE_CONTRACTS, adapter_for, topic_matches_sub)
        print(f"PASS compiled product/service V4 contract: {args.product} ({protocol})")
        return 0
    except (ValueError, KeyError, OSError, ImportError, subprocess.SubprocessError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
