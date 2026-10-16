#!/usr/bin/env python3
"""Read-only validator for the frozen MQTT V4 canonicalization vectors."""

from __future__ import annotations

import hashlib
import json
import math
from decimal import Decimal
from pathlib import Path
import re
import sys


SAFE_INTEGER = 9007199254740991
SNAKE_CASE = re.compile(r"^[a-z][a-z0-9]*(?:_[a-z0-9]+)*$")
PROFILE_VERSION = re.compile(r"^(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})$")


class ContractError(ValueError):
    pass


def reject_duplicate_keys(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ContractError(f"duplicate key: {key}")
        result[key] = value
    return result


def parse_integer(token: str) -> int:
    if token == "-0":
        raise ContractError("negative zero is forbidden")
    value = int(token, 10)
    if abs(value) > SAFE_INTEGER:
        raise ContractError("integer exceeds the safe range")
    return value


def reject_non_integer(token: str):
    raise ContractError(f"non-integer JSON number is forbidden: {token}")


def load_contract_json(text: str):
    return json.loads(
        text,
        object_pairs_hook=reject_duplicate_keys,
        parse_int=parse_integer,
        parse_float=reject_non_integer,
        parse_constant=reject_non_integer,
    )


def utf16_sort_key(value: str) -> bytes:
    return value.encode("utf-16-be")


def parse_number(token: str) -> float:
    value = float(token)
    if (not math.isfinite(value) or (value == 0 and
            (token.startswith("-") or Decimal(token) != 0))):
        raise ContractError("nonfinite, negative zero or underflowed Number")
    return value


def load_number_json(text: str):
    """Numeric conformance input only; Profile field validation is separate.

    Use load_contract_json for Integer schemas. This opts ALL numeric fields
    into binary64 Number, including integer-shaped input outside safe range.
    """
    return json.loads(text, object_pairs_hook=reject_duplicate_keys,
                      parse_int=parse_number, parse_float=parse_number,
                      parse_constant=reject_non_integer)


def canonical_number(value: float) -> str:
    if not math.isfinite(value) or (value == 0 and math.copysign(1, value) < 0):
        raise ContractError("invalid binary64 Number")
    if value == 0:
        return "0"
    # Python's shortest round-trip digits, rendered at ECMAScript thresholds.
    # Tests compare both this reference and the C Ryu codec with ECMAScript.
    negative = value < 0
    mantissa, _, exponent = repr(abs(value)).lower().partition("e")
    integer, _, fraction = mantissa.partition(".")
    digits = (integer + fraction).lstrip("0").rstrip("0")
    point = len(integer) + int(exponent or "0")
    point -= len(integer + fraction) - len((integer + fraction).lstrip("0"))
    if -6 <= point - 1 < 21:
        if point <= 0:
            text = "0." + "0" * -point + digits
        elif point >= len(digits):
            text = digits + "0" * (point - len(digits))
        else:
            text = digits[:point] + "." + digits[point:]
    else:
        text = digits[0] + (("." + digits[1:]) if len(digits) > 1 else "")
        text += "e" + ("+" if point - 1 >= 0 else "-") + str(abs(point - 1))
    return ("-" if negative else "") + text


def canonicalize(value) -> str:
    """RFC 8785 serializer after Integer/Profile Number field validation."""
    if value is None:
        return "null"
    if value is True:
        return "true"
    if value is False:
        return "false"
    if isinstance(value, int):
        if abs(value) > SAFE_INTEGER:
            raise ContractError("integer exceeds the safe range")
        return str(value)
    if isinstance(value, float):
        return canonical_number(value)
    if isinstance(value, str):
        return json.dumps(value, ensure_ascii=False, separators=(",", ":"))
    if isinstance(value, list):
        return "[" + ",".join(canonicalize(item) for item in value) + "]"
    if isinstance(value, dict):
        if not all(isinstance(key, str) for key in value):
            raise ContractError("JSON object keys must be strings")
        fields = (
            canonicalize(key) + ":" + canonicalize(value[key])
            for key in sorted(value, key=utf16_sort_key)
        )
        return "{" + ",".join(fields) + "}"
    raise ContractError(f"unsupported JSON value: {type(value).__name__}")


def normalize_vector(vector):
    value = vector["value"]
    normalization = vector.get("normalization")
    if normalization == "manifest":
        normalized_profiles = []
        for profile in value["profiles"]:
            normalized = dict(profile)
            if "instances" in normalized:
                instances = normalized["instances"]
                if len(instances) != len(set(instances)):
                    raise ContractError("manifest instances must be unique")
                normalized["instances"] = sorted(instances, key=utf16_sort_key)
            normalized_profiles.append(normalized)
        normalized_profiles.sort(key=lambda profile: utf16_sort_key(profile["name"]))
        normalized_value = dict(value)
        normalized_value["profiles"] = normalized_profiles
        return normalized_value
    if normalization is not None:
        raise ContractError(f"unknown normalization: {normalization}")
    return value


def validate_registry(registry) -> list[str]:
    failures = []
    names = {}
    matchers = {}
    command_usages = {}

    def register_operation(kind: str, operation, profile_name: str, is_command: bool) -> None:
        name = operation.get("name")
        matcher = operation.get("operationMatcher", {})
        matcher_key = (matcher.get("field"), matcher.get("equals"))
        if not isinstance(name, str) or len(name) > 64 or not SNAKE_CASE.fullmatch(name):
            failures.append(f"{kind}: invalid operation name {name!r}")
        if matcher.get("equals") != name:
            failures.append(f"{kind} {name}: matcher value must equal the operation name")
        for required in ("introducedVersion", "semanticDescription"):
            if not operation.get(required):
                failures.append(f"{kind} {name}: {required} is required")
        if not PROFILE_VERSION.fullmatch(operation.get("introducedVersion", "")):
            failures.append(f"{kind} {name}: introducedVersion must be MAJOR.MINOR")
        if is_command:
            applicable = operation.get("applicableProfiles")
            if (not isinstance(applicable, list) or not applicable
                    or any(not isinstance(item, str) for item in applicable)
                    or len(applicable) != len(set(applicable))):
                failures.append(f"{kind} {name}: applicableProfiles must be a nonempty unique list")
            elif profile_name not in applicable:
                failures.append(f"{kind} {name}: applicableProfiles excludes {profile_name}")
            command_usages.setdefault(name, set()).add(profile_name)
        elif operation.get("ownerProfile") != profile_name:
            failures.append(f"{kind} {name}: ownerProfile mismatch")

        definition = {key: value for key, value in operation.items() if key != "ownerProfile"}
        previous = names.get(name)
        if previous is not None:
            previous_kind, previous_definition = previous
            if not is_command or previous_kind != "command" or definition != previous_definition:
                failures.append(f"{kind}: conflicting operation name {name}")
        else:
            names[name] = ("command" if is_command else "report", definition)
        previous_matcher = matchers.get(matcher_key)
        if previous_matcher is not None and previous_matcher != name:
            failures.append(f"{kind} {name}: conflicting operationMatcher {matcher_key}")
        else:
            matchers[matcher_key] = name

    if registry.get("protocolVersion") != "4.0":
        failures.append("registry protocolVersion must be 4.0")

    for command in registry.get("core", {}).get("commands", []):
        register_operation("core command", command, "core", True)

    profile_names = set()
    for profile in registry.get("profiles", []):
        name = profile.get("name")
        version = profile.get("currentVersion")
        majors = profile.get("supportedMajors", [])
        if not isinstance(name, str) or len(name) > 64 or not SNAKE_CASE.fullmatch(name):
            failures.append(f"invalid profile name {name!r}")
        elif name in profile_names:
            failures.append(f"duplicate profile name {name}")
        else:
            profile_names.add(name)
        match = PROFILE_VERSION.fullmatch(version or "")
        if not match:
            failures.append(f"profile {name}: invalid currentVersion {version!r}")
        else:
            major, minor = int(match.group(1)), int(match.group(2))
            if major > 65535 or minor > 65535:
                failures.append(f"profile {name}: version component exceeds uint16")
            if major not in majors:
                failures.append(f"profile {name}: current major is not supported")
        if not profile.get("contract"):
            failures.append(f"profile {name}: contract reference is required")
        for command in profile.get("commands", []):
            register_operation(f"profile {name} command", command, name, True)
        for report in profile.get("reports", []):
            register_operation(f"profile {name} report", report, name, False)

    for name, usages in command_usages.items():
        applicable = names[name][1].get("applicableProfiles")
        if isinstance(applicable, list) and set(applicable) != usages:
            failures.append(f"command {name}: applicableProfiles does not match registered profiles")

    return failures


def main() -> int:
    repo = Path(__file__).resolve().parents[1]
    vector_path = repo / "docs" / "protocols" / "v4" / "test-vectors.json"
    registry_path = repo / "docs" / "protocols" / "v4" / "registry.json"
    vectors = load_contract_json(vector_path.read_text(encoding="utf-8"))
    registry = load_contract_json(registry_path.read_text(encoding="utf-8"))

    failures = validate_registry(registry)
    for profile_path in sys.argv[1:]:
        product_registry = load_contract_json(Path(profile_path).read_text(encoding="utf-8"))
        if product_registry.get("schemaVersion") != registry.get("schemaVersion"):
            failures.append(f"{profile_path}: schemaVersion mismatch")
            continue
        if product_registry.get("protocolVersion") != registry.get("protocolVersion"):
            failures.append(f"{profile_path}: protocolVersion mismatch")
            continue
        profile = product_registry.get("profile")
        if not isinstance(profile, dict):
            failures.append(f"{profile_path}: profile object required")
            continue
        failures.extend(validate_registry({**registry, "profiles": [profile]}))
    for vector in vectors["valid"]:
        actual = canonicalize(normalize_vector(vector))
        digest = hashlib.sha256(actual.encode("utf-8")).hexdigest()
        if actual != vector["canonical"]:
            failures.append(f"{vector['name']}: canonical bytes mismatch")
        if digest != vector["sha256"]:
            failures.append(f"{vector['name']}: SHA-256 mismatch")

    for vector in vectors["invalidJson"]:
        try:
            load_contract_json(vector["json"])
        except (ContractError, json.JSONDecodeError):
            continue
        failures.append(f"{vector['name']}: invalid JSON was accepted")

    for vector in vectors.get("numberValid", []):
        actual = canonicalize(load_number_json(vector["json"]))
        if actual != vector["canonical"] or hashlib.sha256(actual.encode()).hexdigest() != vector["sha256"]:
            failures.append(f"{vector['name']}: Number canonical bytes/digest mismatch")
    for vector in vectors.get("numberInvalid", []):
        try:
            load_number_json(vector["json"])
        except (ContractError, json.JSONDecodeError):
            continue
        failures.append(f"{vector['name']}: invalid Number was accepted")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 1

    print(
        f"validated {len(vectors['valid'])} valid and "
        f"{len(vectors['invalidJson'])} invalid MQTT V4 vectors; "
        f"{len(vectors.get('numberValid', []))} valid and "
        f"{len(vectors.get('numberInvalid', []))} invalid Number vectors; "
        f"registry contains {len(registry['profiles'])} profile"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
