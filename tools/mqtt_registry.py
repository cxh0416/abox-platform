"""Registry metadata validation shared by vectors and compiled-descriptor checks.

Schema 1 is the frozen command-level metadata. Schema 2 adds complete operation
contracts. Neither schema changes the public MQTT version or business payloads.
"""
from __future__ import annotations

import json
import math
import re
from pathlib import Path

CLASSIFICATIONS = {"CORE": 0, "QUERY": 1, "DISCRETE_ACTION": 2,
                   "CONTINUOUS_SESSION": 3, "SAFETY_STOP": 4, "PARAMETERIZED": 5}
EXECUTABLE = set(CLASSIFICATIONS) - {"CORE", "PARAMETERIZED"}
NAME = re.compile(r"^[a-z][a-z0-9]*(?:_[a-z0-9]+)*$")
VERSION = re.compile(r"^(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})$")
FIELD = re.compile(r"^data\.(?:command|params(?:\.[A-Za-z_][A-Za-z0-9_]{0,63})*)$")
SAFE_INTEGER = 9007199254740991
MAX_DEPTH, MAX_NODES, MAX_CLAUSES = 8, 64, 256


class RegistryError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise RegistryError(message)


def unique_object(pairs):
    obj = {}
    for key, value in pairs:
        require(key not in obj, f"duplicate JSON key {key!r}")
        obj[key] = value
    return obj


def parse_integer(token):
    value = int(token)
    require(token != "-0" and abs(value) <= SAFE_INTEGER, f"invalid Integer {token}")
    return value


def parse_number(token):
    value = float(token)
    # The decimal significand, rather than float(token), distinguishes a true
    # zero from a nonzero value that underflowed. No external JSON package.
    from decimal import Decimal
    require(math.isfinite(value) and not (value == 0 and
            (token.startswith("-") or Decimal(token) != 0)), f"invalid Number {token}")
    return value


def invalid_constant(token):
    raise RegistryError(f"invalid JSON constant {token}")


def _strings(value):
    if isinstance(value, str):
        require("\0" not in value, "Registry metadata cannot contain NUL")
        try:
            value.encode("utf-8")
        except UnicodeEncodeError as exc:
            raise RegistryError("invalid Unicode in Registry metadata") from exc
    elif isinstance(value, dict):
        for key, item in value.items():
            _strings(key)
            _strings(item)
    elif isinstance(value, list):
        for item in value:
            _strings(item)


def loads(text):
    try:
        value = json.loads(text, object_pairs_hook=unique_object,
                           parse_int=parse_integer, parse_float=parse_number,
                           parse_constant=invalid_constant)
        _strings(value)
        return value
    except (json.JSONDecodeError, UnicodeError, OverflowError, RecursionError) as exc:
        raise RegistryError(f"invalid Registry JSON: {exc}") from exc


def load(path):
    try:
        return loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, UnicodeError) as exc:
        raise RegistryError(f"cannot read {path}: {exc}") from exc


def name(value):
    return isinstance(value, str) and len(value) <= 64 and bool(NAME.fullmatch(value)) and value.count("_") < 8


def version(value):
    match = VERSION.fullmatch(value) if isinstance(value, str) else None
    require(match is not None, f"invalid version {value!r}")
    pair = tuple(map(int, match.groups()))
    require(max(pair) <= 65535, f"version component exceeds uint16: {value}")
    return pair


def integer(value, minimum=0, maximum=65535):
    return type(value) is int and minimum <= value <= maximum


def nonempty(value):
    return isinstance(value, str) and bool(value.strip())


def object_fields(obj, fields, label):
    require(isinstance(obj, dict), f"{label}: object required")
    missing, extra = fields - obj.keys(), obj.keys() - fields
    require(not missing, f"{label}: missing fields {sorted(missing)}")
    require(not extra, f"{label}: unsupported fields {sorted(extra)}")


def scalar(value):
    if value is None:
        return ("null",)
    if type(value) is bool:
        return ("boolean", value)
    if isinstance(value, str):
        return ("string", value)
    require(type(value) in (int, float) and math.isfinite(value), "matcher equals must be a finite scalar")
    if type(value) is float and value.is_integer() and abs(value) <= SAFE_INTEGER:
        value = int(value)
    return ("number", value)


def merge(left, right):
    out = dict(left)
    for field, value in right.items():
        for other, existing in out.items():
            if other == field and existing != value:
                return None
            # Every allowed equality value is a scalar, so it cannot have a
            # simultaneously matched object descendant.
            if other != field and (other.startswith(field + ".") or field.startswith(other + ".")):
                return None
        out[field] = value
    return out


def matcher_clauses(matcher, command):
    nodes = 0

    def visit(node, depth):
        nonlocal nodes
        nodes += 1
        require(depth <= MAX_DEPTH and nodes <= MAX_NODES, "operationMatcher exceeds depth/node budget")
        require(isinstance(node, dict), "operationMatcher must be an object or null")
        if set(node) == {"field", "equals"}:
            path = node["field"]
            require(isinstance(path, str) and len(path) <= 128 and FIELD.fullmatch(path),
                    f"invalid operationMatcher field {path!r}")
            return [{path: scalar(node["equals"])}]
        require(set(node) in ({"allOf"}, {"anyOf"}), "invalid operationMatcher keys")
        mode = next(iter(node))
        children = node[mode]
        require(isinstance(children, list) and children and len(children) <= MAX_NODES,
                f"{mode}: nonempty matcher list required")
        clauses = [{}] if mode == "allOf" else []
        for child in children:
            options = visit(child, depth + 1)
            if mode == "anyOf":
                clauses.extend(options)
            else:
                clauses = [joined for a in clauses for b in options
                           if (joined := merge(a, b)) is not None]
            require(len(clauses) <= MAX_CLAUSES, "operationMatcher expands beyond clause budget")
        return clauses

    clauses = [{}] if matcher is None else visit(matcher, 1)
    clauses = [joined for clause in clauses
               if (joined := merge(clause, {"data.command": ("string", command)})) is not None]
    require(clauses, f"{command}: unreachable operationMatcher")
    unique = {tuple(sorted(clause.items())) for clause in clauses}
    # Boolean order and redundant branches are not semantic differences.
    return tuple(sorted((clause for clause in unique
                         if not any(set(other) < set(clause) for other in unique)), key=repr))


def overlaps(a, b):
    return any(merge(dict(left), dict(right)) is not None for left in a for right in b)


def schema(value, label, nullable=False):
    require((nullable and value is None) or isinstance(value, dict) or type(value) is bool,
            f"{label}: JSON Schema object/boolean required")
    return value


def normalize_operation(op, command):
    object_fields(op, {"operationMatcher", "classification", "expiresAt", "maxValidity",
                       "params", "result", "200Boundary", "rebootPolicy", "errorCodes"}, command)
    require(op["classification"] in EXECUTABLE, f"{command}: invalid executable classification")
    policy, maximum = op["expiresAt"], op["maxValidity"]
    require(policy in ("required", "optional", "forbidden") and integer(maximum, 0, 0xffffffff),
            f"{command}: invalid expiry policy/maxValidity")
    require(policy != "forbidden" or maximum == 0, f"{command}: forbidden expiry must have maxValidity 0")
    require(policy != "required" or maximum > 0, f"{command}: required expiry needs maxValidity")
    if op["classification"] == "DISCRETE_ACTION":
        require(policy == "required" and 0 < maximum <= 30000, f"{command}: discrete action requires validity <= 30000")
    require(nonempty(op["200Boundary"]) and nonempty(op["rebootPolicy"]), f"{command}: success/reboot policy required")
    codes = op["errorCodes"]
    require(isinstance(codes, list) and all(integer(x, 1) and x != 200 for x in codes) and
            len(set(codes)) == len(codes), f"{command}: invalid or duplicate errorCodes")
    return {**op, "operationMatcher": matcher_clauses(op["operationMatcher"], command),
            "params": schema(op["params"], f"{command}.params"),
            "result": schema(op["result"], f"{command}.result", nullable=True), "errorCodes": sorted(codes)}


def normalize_profile(profile, revision):
    require(isinstance(profile, dict), "profile object required")
    label = profile.get("name")
    require(name(label) and label != "core", f"invalid profile name {label!r}")
    current = version(profile.get("currentVersion"))
    majors = profile.get("supportedMajors")
    require(isinstance(majors, list) and majors and all(integer(x) for x in majors) and
            len(set(majors)) == len(majors) and current[0] in majors, f"{label}: invalid supportedMajors")
    require(nonempty(profile.get("contract")), f"{label}: contract reference required")
    if revision == 2:
        object_fields(profile, {"name", "currentVersion", "supportedMajors", "contract", "commands", "reports"}, label)
    commands, reports = {}, {}
    require(isinstance(profile.get("commands", []), list) and isinstance(profile.get("reports", []), list),
            f"{label}: command/report lists required")
    for command in profile.get("commands", []):
        require(isinstance(command, dict), f"{label}: command object required")
        cmd = command.get("name")
        require(name(cmd) and cmd != "sync_state" and cmd not in commands, f"{label}: duplicate/reserved/invalid command {cmd!r}")
        introduced = version(command.get("introducedVersion"))
        require(introduced <= current, f"{label}.{cmd}: introducedVersion exceeds profile version")
        require(nonempty(command.get("semanticDescription")), f"{cmd}: semanticDescription required")
        applicable = command.get("applicableProfiles")
        require(isinstance(applicable, list) and applicable and all(name(x) for x in applicable) and
                len(set(applicable)) == len(applicable) and label in applicable, f"{cmd}: invalid applicableProfiles")
        if revision == 1:
            require(command.get("classification") in EXECUTABLE, f"{cmd}: invalid legacy classification")
            require(command.get("operationMatcher") == {"field": "data.command", "equals": cmd},
                    f"{cmd}: invalid legacy command matcher")
            commands[cmd] = {**command, "applicableProfiles": sorted(applicable)}
        else:
            object_fields(command, {"name", "introducedVersion", "semanticDescription", "applicableProfiles",
                                    "classification", "operations"}, cmd)
            ops = command["operations"]
            require(isinstance(ops, list) and ops, f"{cmd}: nonempty operations required")
            normalized = [normalize_operation(op, cmd) for op in ops]
            for i, op in enumerate(normalized):
                require(not any(overlaps(op["operationMatcher"], other["operationMatcher"]) for other in normalized[:i]),
                        f"{cmd}: overlapping operationMatcher branches")
            categories = {op["classification"] for op in normalized}
            projected = next(iter(categories)) if len(categories) == 1 else "PARAMETERIZED"
            require(command["classification"] == projected, f"{cmd}: descriptor classification must be {projected}")
            commands[cmd] = {**command, "applicableProfiles": sorted(applicable),
                             "operations": sorted(normalized, key=lambda op: repr(op["operationMatcher"]))}
    for report in profile.get("reports", []):
        require(isinstance(report, dict), f"{label}: report object required")
        report_name = report.get("name")
        require(name(report_name) and report_name not in reports, f"{label}: duplicate/invalid report {report_name!r}")
        require(report.get("ownerProfile") == label, f"{report_name}: ownerProfile mismatch")
        require(version(report.get("introducedVersion")) <= current, f"{report_name}: introducedVersion exceeds profile version")
        require(nonempty(report.get("semanticDescription")), f"{report_name}: semanticDescription required")
        require(report.get("reportType") in ("state", "event", "telemetry"), f"{report_name}: invalid reportType")
        require(report.get("operationMatcher") == {"field": "data.reportName", "equals": report_name},
                f"{report_name}: invalid report matcher")
        if revision == 2:
            object_fields(report, {"name", "ownerProfile", "introducedVersion", "semanticDescription", "reportType",
                                   "operationMatcher", "payload", "fullState", "instanceId", "stream", "requestCorrelation"}, report_name)
            schema(report["payload"], f"{report_name}.payload")
            require(type(report["fullState"]) is bool and type(report["requestCorrelation"]) is bool,
                    f"{report_name}: fullState/requestCorrelation must be Boolean")
            require(not report["fullState"] or report["reportType"] == "state", f"{report_name}: fullState requires state")
            require(report["instanceId"] in ("required", "forbidden") and report["stream"] in ("strict", "none"),
                    f"{report_name}: invalid instance/stream policy")
        reports[report_name] = report
    return {**profile, "supportedMajors": sorted(majors), "commands": commands, "reports": reports}


def normalize_registry(registry, *, compiled=False):
    require(isinstance(registry, dict), "Registry object required")
    revision = registry.get("schemaVersion")
    require(type(revision) is int and revision in (1, 2), "unsupported Registry schemaVersion")
    require(registry.get("protocolVersion") == "4.0", "registry protocolVersion must be 4.0")
    if revision == 2:
        require(not (registry.keys() - {"schemaVersion", "protocolVersion", "profile", "profiles", "core"}),
                "unsupported Registry root fields")
        require("profile" in registry or "profiles" in registry, "Profile list required")
    core = registry.get("core", {})
    require(isinstance(core, dict) and isinstance(core.get("commands", []), list), "invalid Core registry")
    seen_core = set()
    for cmd in core.get("commands", []):
        require(isinstance(cmd, dict) and cmd.get("name") == "sync_state" and "sync_state" not in seen_core and
                cmd.get("classification") == "CORE" and cmd.get("operationMatcher") == {"field": "data.command", "equals": "sync_state"} and
                cmd.get("applicableProfiles") == ["core"] and cmd.get("introducedVersion") == "4.0" and nonempty(cmd.get("semanticDescription")),
                "invalid/duplicate Core sync_state registration")
        seen_core.add("sync_state")
    require(not ("profile" in registry and "profiles" in registry), "use profile or profiles, not both")
    profiles = [registry["profile"]] if "profile" in registry else registry.get("profiles", [])
    require(isinstance(profiles, list) and (not compiled or len(profiles) <= 16), "invalid Profile list")
    normalized, reports, usages, definitions = {}, set(), {}, {}
    for profile in profiles:
        p = normalize_profile(profile, revision)
        require(p["name"] not in normalized, f"duplicate profile name {p['name']}")
        normalized[p["name"]] = p
        for report_name in p["reports"]:
            require(report_name not in reports, f"duplicate global reportName {report_name}")
            reports.add(report_name)
        for cmd, value in p["commands"].items():
            require(cmd not in reports, f"command/report name collision {cmd}")
            identity = value["introducedVersion"], value["semanticDescription"]
            require(cmd not in definitions or definitions[cmd] == identity, f"{cmd}: conflicting global command semantics")
            definitions[cmd] = identity
            usages.setdefault(cmd, set()).add(p["name"])
    require(not (set(usages) & reports), "global command/report name collision")
    for p in normalized.values():
        for cmd, value in p["commands"].items():
            require(set(value["applicableProfiles"]) == usages[cmd], f"{cmd}: applicableProfiles does not match registered profiles")
    if compiled and revision == 2:
        for cmd in usages:
            rules = [(p["name"], op["operationMatcher"]) for p in normalized.values()
                     for op in p["commands"].get(cmd, {}).get("operations", [])]
            for i, (profile, matcher) in enumerate(rules):
                require(not any(other != profile and overlaps(matcher, previous) for other, previous in rules[:i]),
                        f"{cmd}: ambiguous routing across enabled Profiles")
    return {"schemaVersion": revision, "protocolVersion": "4.0", "profiles": normalized}


def validate_registry(registry):
    try:
        normalize_registry(registry)
        return []
    except (RegistryError, TypeError, KeyError) as exc:
        return [str(exc)]


def same_json(a, b):
    """JSON equality with exact Boolean typing and semantic numeric equality."""
    if type(a) is bool or type(b) is bool:
        return type(a) is type(b) and a == b
    if type(a) in (int, float) and type(b) in (int, float):
        return a == b
    if type(a) is not type(b):
        return False
    if isinstance(a, dict):
        return a.keys() == b.keys() and all(same_json(a[k], b[k]) for k in a)
    if isinstance(a, (list, tuple)):
        return len(a) == len(b) and all(same_json(x, y) for x, y in zip(a, b))
    return a == b
