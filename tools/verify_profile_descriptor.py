"""Compare Registry JSON with the compiled production descriptor/operation table."""
from __future__ import annotations

import argparse
import subprocess
import sys

from mqtt_registry import (CLASSIFICATIONS, RegistryError, load, loads,
                           normalize_registry, require, same_json)


def legacy_rows(registry):
    rows = []
    for name, profile in sorted(registry["profiles"].items()):
        rows.append(("profile", name, profile["currentVersion"]))
        rows.extend(("command", name, command, str(CLASSIFICATIONS[item["classification"]]))
                    for command, item in sorted(profile["commands"].items()))
        rows.extend(("report", name, report, item["reportType"])
                    for report, item in sorted(profile["reports"].items()))
    return sorted(rows)


def parse_legacy(text):
    rows, profile, seen = [], None, set()
    for line in text.splitlines():
        fields = tuple(line.split("\t"))
        require(len(fields) == 3, "malformed legacy descriptor row")
        kind, name, value = fields
        require(kind in ("profile", "command", "report"), f"unknown legacy descriptor kind {kind!r}")
        if kind == "profile":
            profile = name
            row = fields
        else:
            require(profile is not None, "legacy command/report precedes profile")
            row = (kind, profile, name, value)
        key = row[:-1]
        require(key not in seen, f"duplicate legacy descriptor row {key}")
        seen.add(key)
        rows.append(row)
    require(rows, "empty compiled descriptor")
    return sorted(rows)


def differences(expected, actual, path="profiles", limit=8):
    result = []
    if same_json(expected, actual):
        return result
    if isinstance(expected, dict) and isinstance(actual, dict):
        for key in sorted(expected.keys() | actual.keys()):
            location = f"{path}.{key}"
            if key not in expected:
                result.append(f"{location}: unexpected compiled entry")
            elif key not in actual:
                result.append(f"{location}: missing compiled entry")
            else:
                result.extend(differences(expected[key], actual[key], location, limit))
            if len(result) >= limit:
                break
    elif isinstance(expected, (tuple, list)) and isinstance(actual, (tuple, list)):
        if len(expected) != len(actual):
            result.append(f"{path}: expected {len(expected)} entries, compiled {len(actual)}")
        for index, (left, right) in enumerate(zip(expected, actual)):
            result.extend(differences(left, right, f"{path}[{index}]", limit))
            if len(result) >= limit:
                break
    else:
        result.append(f"{path}: expected {expected!r}, compiled {actual!r}")
    return result[:limit]


def verify(registry, compiled_output, require_complete=False):
    expected = normalize_registry(registry, compiled=True)
    complete = require_complete or expected["schemaVersion"] == 2
    require(not complete or expected["schemaVersion"] == 2,
            "complete verification requires schemaVersion 2 operation/report contracts")
    if compiled_output.lstrip().startswith("{"):
        actual = normalize_registry(loads(compiled_output), compiled=True)
        require(actual["schemaVersion"] == 2, "structured C export must use schemaVersion 2")
        if complete:
            diff = differences(expected["profiles"], actual["profiles"])
            require(not diff, "Registry/C contract mismatch:\n" + "\n".join(diff))
        else:
            require(legacy_rows(expected) == legacy_rows(actual), "legacy Registry/C descriptor mismatch")
    else:
        require(not complete, "complete verification requires the compiled operation-table JSON export; legacy TSV is insufficient")
        require(legacy_rows(expected) == parse_legacy(compiled_output), "legacy Registry/C descriptor mismatch")
    return "complete operation/report contracts" if complete else "legacy names/categories/report types only"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("registry_json")
    parser.add_argument("test_executable")
    parser.add_argument("--require-complete", action="store_true",
                        help="refuse legacy command-only coverage (default for schemaVersion 2)")
    args = parser.parse_args(argv)
    try:
        registry = load(args.registry_json)
        normalize_registry(registry, compiled=True)
        ran = subprocess.run([args.test_executable, "--describe"], capture_output=True,
                             text=True, encoding="utf-8", check=False, timeout=30)
        require(ran.returncode == 0, f"compiled descriptor rejected its contracts (exit {ran.returncode}): {ran.stderr[:2000]}")
        scope = verify(registry, ran.stdout, args.require_complete)
        print(f"verified {scope}: {args.registry_json}")
        return 0
    except (RegistryError, OSError, UnicodeError, subprocess.TimeoutExpired, TypeError, KeyError, RecursionError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
