import copy
from pathlib import Path
import sys
from types import SimpleNamespace as NS
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from verify_v4_product import verify


class PreflightTest(unittest.TestCase):
    def setUp(self):
        self.wire = {"packets": [
            {"topic": f"device/BENCH/{suffix}", "qos": qos, "retain": retain,
             "payload": {"version": "4.0", "vid": "BENCH", "firmware": "bench"}}
            for suffix, qos, retain in (("manifest", 1, 1), ("heartbeat", 0, 0), ("report", 1, 0))]}
        self.identity = {"hardwareContract": "wing", "tlsProfileId": 0, "requirePsk": 0}
        self.contracts = {"wing": NS(product="wing", mqtt=NS(tls_profile_id="0", psk_enrollment=False))}
        self.adapter = NS(subscription_specs=[("device/#", 1)], parse=self.parse)

    @staticmethod
    def parse(topic, raw):
        import json
        data = json.loads(raw)
        suffix = topic.rsplit("/", 1)[-1]
        return NS(kind={"manifest": "manifest", "heartbeat": "heartbeat", "report": "status_report"}[suffix],
                  vid=data["vid"], fw_version=data["firmware"])

    def check(self):
        return verify("wing", self.wire, self.identity, {"wing": {"protocol": "v4"}},
                      self.contracts, lambda *_: self.adapter, lambda pattern, topic: topic.startswith(pattern[:-1]))

    def test_accept_compatible_bootstrap(self):
        self.assertEqual(self.check(), "v4")

    def test_reject_missing_enrollment_and_tls_mismatch(self):
        self.contracts.clear()
        with self.assertRaisesRegex(ValueError, "hardware contract"):
            self.check()
        self.setUp()
        self.identity["tlsProfileId"] = 1
        with self.assertRaisesRegex(ValueError, "TLS profile"):
            self.check()

    def test_reject_old_service_adapter_before_board_test(self):
        self.adapter.parse = lambda *_: None
        with self.assertRaisesRegex(ValueError, "adapter rejects"):
            self.check()

    def test_reject_missing_subscription_and_low_qos(self):
        for specs in ([], [("device/#", 0)]):
            self.adapter.subscription_specs = specs
            with self.assertRaisesRegex(ValueError, "subscribe"):
                self.check()

    def test_reject_partial_bootstrap_and_identity_drift(self):
        original = copy.deepcopy(self.wire)
        self.wire["packets"].pop()
        with self.assertRaisesRegex(ValueError, "must contain"):
            self.check()
        self.wire = original
        self.wire["packets"][1]["payload"]["vid"] = "OTHER"
        with self.assertRaisesRegex(ValueError, "identity or firmware"):
            self.check()


if __name__ == "__main__":
    unittest.main()
