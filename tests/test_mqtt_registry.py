"""Contract drift must fail against the executable's real production tables."""
from __future__ import annotations
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from mqtt_registry import (RegistryError, load, loads, matcher_clauses, normalize_registry,
                           overlaps, same_json, validate_registry)
from verify_profile_descriptor import verify
from validate_mqtt_v4 import ContractError, load_contract_json

EXECUTABLE, LEGACY = sys.argv[1:3]
sys.argv = [sys.argv[0]]


class RegistryContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.registry = load(ROOT / "tests/fixtures/v4/registry-contract.json")
        cls.legacy = load(ROOT / "tests/fixtures/v4/registry-legacy.json")
        cls.compiled = subprocess.run([EXECUTABLE, "--describe"], capture_output=True,
                                      text=True, encoding="utf-8", check=True).stdout
        cls.compiled_legacy = subprocess.run([LEGACY, "--describe"], capture_output=True,
                                             text=True, encoding="utf-8", check=True).stdout

    def test_actual_compiled_tables(self):
        self.assertIn("complete", verify(self.registry, self.compiled, True))

    def test_each_contract_surface_detects_drift(self):
        mutations = {
            "maxValidity": lambda p: p["commands"][0]["operations"][0].update(maxValidity=4000),
            "matcher": lambda p: p["commands"][0]["operations"][0]["operationMatcher"]["allOf"][0].update(equals="changed"),
            "params": lambda p: p["commands"][0]["operations"][0]["params"].update(additionalProperties=False),
            "result": lambda p: p["commands"][1]["operations"][0].update(result=False),
            "boundary": lambda p: p["commands"][0]["operations"][0].update({"200Boundary": "different success"}),
            "reboot": lambda p: p["commands"][0]["operations"][0].update(rebootPolicy="session_constraint"),
            "errors": lambda p: p["commands"][0]["operations"][0].update(errorCodes=[4003]),
            "version": lambda p: p.update(currentVersion="1.1"),
            "contract": lambda p: p.update(contract="fixture://changed"),
            "supportedMajors": lambda p: p.update(supportedMajors=[0, 1]),
            "reportType": lambda p: p["reports"][1].update(reportType="event"),
            "payload": lambda p: p["reports"][0]["payload"]["properties"]["position"].update(multipleOf=0.2),
            "fullState": lambda p: p["reports"][0].update(fullState=False),
            "instance": lambda p: p["reports"][0].update(instanceId="required"),
            "stream": lambda p: p["reports"][0].update(stream="none"),
            "correlation": lambda p: p["reports"][0].update(requestCorrelation=False),
            "report version": lambda p: p["reports"][0].update(introducedVersion="0.9"),
            "report description": lambda p: p["reports"][0].update(semanticDescription="Changed observation."),
        }
        for label, mutate in mutations.items():
            with self.subTest(label=label):
                changed = copy.deepcopy(self.registry)
                mutate(changed["profiles"][0])
                self.assertEqual(validate_registry(changed), [], label)
                with self.assertRaises(RegistryError):
                    verify(changed, self.compiled, True)

    def test_policy_and_descriptor_projection_are_not_independent(self):
        for policy in ("optional", "forbidden"):
            changed = copy.deepcopy(self.registry)
            changed["profiles"][0]["commands"][0]["operations"][0]["expiresAt"] = policy
            self.assertTrue(validate_registry(changed))
        changed = loads(self.compiled)
        changed["profiles"][0]["commands"][0]["classification"] = "DISCRETE_ACTION"
        with self.assertRaisesRegex(RegistryError, "PARAMETERIZED"):
            verify(self.registry, json.dumps(changed))
        changed = copy.deepcopy(self.registry)
        command = changed["profiles"][0]["commands"][1]
        command["classification"] = command["operations"][0]["classification"] = "CONTINUOUS_SESSION"
        self.assertEqual(validate_registry(changed), [])
        with self.assertRaisesRegex(RegistryError, "classification"):
            verify(changed, self.compiled)

    def test_no_missing_or_extra_compiled_descriptors(self):
        for path in ("profiles", "commands", "reports", "operations"):
            for extra in (False, True):
                changed = loads(self.compiled)
                target = changed["profiles"]
                if path != "profiles":
                    target = target[0]["commands" if path in ("commands", "operations") else "reports"]
                    if path == "operations":
                        target = target[0]["operations"]
                target.append(copy.deepcopy(target[0])) if extra else target.pop()
                with self.subTest(path=path, extra=extra), self.assertRaises(RegistryError):
                    verify(self.registry, json.dumps(changed))

    def test_set_and_matcher_order_are_not_contract_drift(self):
        changed = copy.deepcopy(self.registry)
        for p in changed["profiles"]:
            for c in p["commands"]:
                c["applicableProfiles"].reverse()
                c["operations"].reverse()
                for op in c["operations"]:
                    op["errorCodes"].reverse()
                    if op["operationMatcher"] and "allOf" in op["operationMatcher"]:
                        op["operationMatcher"]["allOf"].reverse()
            p["commands"].reverse()
            p["reports"].reverse()
        changed["profiles"].reverse()
        verify(changed, self.compiled)

    def test_overlap_unreachable_and_global_routing(self):
        changed = copy.deepcopy(self.registry)
        rules = changed["profiles"][0]["commands"][0]["operations"]
        rules[1]["operationMatcher"] = None
        self.assertIn("overlapping", validate_registry(changed)[0])
        changed = copy.deepcopy(self.registry)
        changed["profiles"][1]["commands"][0]["operations"][0]["operationMatcher"] = changed["profiles"][0]["commands"][0]["operations"][0]["operationMatcher"]
        with self.assertRaisesRegex(RegistryError, "across enabled Profiles"):
            normalize_registry(changed, compiled=True)
        with self.assertRaisesRegex(RegistryError, "unreachable"):
            matcher_clauses({"field": "data.command", "equals": "other"}, "command")
        true = matcher_clauses({"field": "data.params.flag", "equals": True}, "command")
        one = matcher_clauses({"field": "data.params.flag", "equals": 1}, "command")
        self.assertFalse(overlaps(true, one))
        self.assertTrue(overlaps(one, matcher_clauses({"field": "data.params.flag", "equals": 1.0}, "command")))
        with self.assertRaises(RegistryError):
            matcher_clauses({"allOf": [{"field": "data.params.x", "equals": None},
                                       {"field": "data.params.x.y", "equals": 1}]}, "command")

    def test_metadata_bounds_and_reserved_core(self):
        for key, value in (("name", "bad__name"), ("currentVersion", "01.0"),
                           ("supportedMajors", [True]), ("supportedMajors", [1, 1])):
            changed = copy.deepcopy(self.registry)
            changed["profiles"][0][key] = value
            self.assertTrue(validate_registry(changed))
        for maximum in (True, -1, 30001, 0, 2**32):
            changed = copy.deepcopy(self.registry)
            changed["profiles"][0]["commands"][0]["operations"][0]["maxValidity"] = maximum
            self.assertTrue(validate_registry(changed))
        changed = copy.deepcopy(self.registry)
        changed["profiles"][0]["commands"][0]["name"] = "sync_state"
        self.assertTrue(validate_registry(changed))
        matcher = {"field": "data.params.action", "equals": "x"}
        for _ in range(9):
            matcher = {"allOf": [matcher]}
        with self.assertRaises(RegistryError):
            matcher_clauses(matcher, "command")

    def test_strict_json_and_number_schemas(self):
        self.assertFalse(same_json({"const": True}, {"const": 1}))
        self.assertTrue(same_json({"minimum": 1.0}, {"minimum": 1}))
        for text in ('{"a":1,"a":2}', '{"x":-0}', '{"x":1e999}', '{"x":-0.0}', '{"x":1e-999}',
                     '{"x":"\\ud800"}', '{"x":"\\u0000"}'):
            with self.subTest(text=text), self.assertRaises(RegistryError):
                loads(text)
        # Accepting fractional Schema bounds must not weaken wire Integer rules.
        self.assertEqual(loads('{"multipleOf":0.1}')["multipleOf"], 0.1)
        with self.assertRaises(ContractError):
            load_contract_json('{"timestamp":1.0}')

    def test_legacy_coverage_is_explicit_and_cannot_satisfy_full_check(self):
        self.assertIn("legacy", verify(self.legacy, self.compiled_legacy))
        with self.assertRaisesRegex(RegistryError, "schemaVersion 2"):
            verify(self.legacy, self.compiled_legacy, True)
        with self.assertRaisesRegex(RegistryError, "legacy TSV"):
            verify(self.registry, self.compiled_legacy)
        with self.assertRaises(RegistryError):
            verify(self.legacy, self.compiled_legacy + self.compiled_legacy)

    def test_cli_rejects_bad_input_before_executable_and_preserves_legacy_entry(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "build/host") as folder:
            path = Path(folder) / "registry.json"
            path.write_text('{"schemaVersion":2,"schemaVersion":1}', encoding="utf-8")
            ran = subprocess.run([sys.executable, str(ROOT / "tools/verify_profile_descriptor.py"),
                                  str(path), "nonexistent-executable"], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 1)
            self.assertIn("duplicate JSON key", ran.stderr)
            self.assertNotIn("Traceback", ran.stderr)
        ran = subprocess.run([sys.executable, str(ROOT / "tools/verify_profile_descriptor.py"),
                              str(ROOT / "tests/fixtures/v4/registry-legacy.json"), LEGACY], capture_output=True, text=True)
        self.assertEqual(ran.returncode, 0, ran.stderr)
        self.assertIn("legacy names/categories/report types only", ran.stdout)


if __name__ == "__main__":
    unittest.main()
