"""Device C serializer vs frozen vectors and platform reference serializer."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from validate_mqtt_v4 import canonicalize, normalize_vector


def run(exe, payload, mode="value", valid=True):
    result = subprocess.run([exe, mode], input=payload, capture_output=True)
    assert result.returncode == (0 if valid else 2), (payload, result.stderr, result.stdout)
    return result.stdout.replace(b"\r\n", b"\n").decode("utf-8").splitlines()


def main(exe):
    vectors = json.loads((ROOT / "docs/protocols/v4/test-vectors.json").read_text(encoding="utf-8"))
    for vector in vectors["valid"]:
        value = normalize_vector(vector)
        digest, text = run(exe, json.dumps(value, ensure_ascii=False).encode())
        assert text == vector["canonical"] and digest == vector["sha256"], vector["name"]
    for vector in vectors["invalidJson"]:
        run(exe, vector["json"].encode(), valid=False)
    for raw in [b'{"a":1,"\\u0061":2}', b'{"a":"\\ud800"}', b'{"a":"\\u0000"}',
                b'{"a":"\xc0\x80"}', b'{"a":01}', b'{"a":true}tail',
                b'[' * 17 + b'0' + b']' * 17, b'{"a":0}\x00']:
        run(exe, raw, valid=False)
    for value in [{"\U0001f600": 1, "\ue000": 2, "\r": "\b\t\n\f\r\x01/\\\""},
                  {"a": -9007199254740991, "b": [True, False, None]},
                  {"e\u0301": "é", "é": "e\u0301"}]:
        expected = canonicalize(value)
        digest, text = run(exe, json.dumps(value, ensure_ascii=False).encode())
        assert text == expected and digest == hashlib.sha256(expected.encode()).hexdigest()
    base = {"command": "power_on", "params": None, "expiresAt": 1788838338000}
    expected = hashlib.sha256(canonicalize(base).encode()).hexdigest()
    for request in [base, dict(base, requestId="first", timestamp=1788838333000),
                    dict(reversed(list(dict(base, requestId="retry", timestamp=1788838333001).items())))]:
        assert run(exe, json.dumps(request).encode(), "request")[0] == expected
    for changed in [dict(base, expiresAt=1788838338001), dict(base, params={}), dict(base, command="power_off")]:
        assert run(exe, json.dumps(changed).encode(), "request")[0] != expected
    # Actual former Patrol FNV collision: common digest keeps these distinct.
    pair = [(134893414, -362, -13), (2828726036, 442, 26)]
    hashes = []
    for seq, rpm, angle in pair:
        request = {"command": "chassis_control", "params": {"action": "acquire", "sessionId": "collision-session",
                   "seq": seq, "leaseMs": 1000, "driveRpm": rpm, "steeringDeg": angle}}
        digest = run(exe, json.dumps(request).encode(), "request")[0]
        assert digest == hashlib.sha256(canonicalize(request).encode()).hexdigest()
        hashes.append(digest)
    assert hashes[0] != hashes[1]
    print("PASS frozen vectors, strict wire validation, UTF-16 ordering, request identity and real collision")


if __name__ == "__main__":
    main(sys.argv[1])
