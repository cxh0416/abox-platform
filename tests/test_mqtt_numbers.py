"""Profile Number lexical, binary64 and JCS tests against ECMAScript."""
import hashlib
import json
import math
from pathlib import Path
import random
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from validate_mqtt_v4 import canonicalize, load_number_json


def run(exe, raw, mode="number", valid=True):
    result = subprocess.run([exe, mode], input=raw.encode(), capture_output=True)
    assert result.returncode == (0 if valid else 2), (raw, result.stdout, result.stderr)
    return result.stdout.replace(b"\r\n", b"\n").decode().splitlines()


def main(exe):
    vectors = json.loads((ROOT / "docs/protocols/v4/test-vectors.json").read_text(encoding="utf-8"))
    for item in vectors["numberValid"]:
        digest, text = run(exe, item["json"])
        assert text == item["canonical"] and digest == item["sha256"], item
    for item in vectors["numberInvalid"]:
        run(exe, item["json"], valid=False)
    # Only declared Number nodes are opted in. Public Integer fields and
    # unknown fields retain raw-token checks even when they round to integers.
    for key in ["timestamp", "expiresAt", "sequence", "code", "count", "extension"]:
        for token in ["1.0", "1e0", "1.00000000000000001", "9007199254740992", "-0"]:
            run(exe, '{"value":1.5,"' + key + '":' + token + '}', valid=False)
    for raw in ['{"value":01}', '{"value":+1}', '{"value":1.}', '{"value":.1}',
                '{"value":1e}', '{"value":1e+}', '{"value":1e--2}',
                '{"value":0x1}', '{"value":1,"\\u0076alue":2}',
                '\ufeff{"value":1}', '\v{"value":1}', '{"value":"\\ud800"}']:
        run(exe, raw, valid=False)
    # Null vs object and distinct binary64 values must not collapse. Decimal
    # spelling changes and outer timestamp changes must retain request identity.
    digests = []
    for token in ["1.5", "1.500", "15e-1"]:
        raw = '{"command":"example_action","params":{"value":' + token + '},"timestamp":42}'
        digests.append(run(exe, raw, "number-request")[0])
    assert len(set(digests)) == 1
    assert digests[0] == "b0dd17a524b8711925f45327f2eaa1d878a91a46cf54f487b24ded37dac2fe24"
    assert run(exe, '{"command":"example_action","params":{"value":1.5000000000000002}}',
               "number-request")[0] != digests[0]
    nested = '{"timestamp":42,"params":{"text":"99\\\"2","values":[1.500,1e-7,1e21],"count":2}}'
    digest, text = run(exe, nested)
    expected_nested = canonicalize(json.loads(nested))
    assert text == expected_nested and digest == hashlib.sha256(text.encode()).hexdigest()
    run(exe, nested.replace('"count":2', '"count":2.0'), valid=False)
    run(exe, '{"params":{"values":[{"value":1.5}]}}', valid=False)
    run(exe, '{"value":1.5}', mode="value", valid=False)  # Legacy parser stays strict.

    # IEEE bit patterns cover adjacent values around all formatting thresholds,
    # extremes, subnormals, ties and a deterministic random distribution.
    rng = random.Random(8785)
    values = [0.0, 5e-324, 1.7976931348623157e308, 333333333.33333329,
              1424953923781206.25, 1e23, 1e-6, 1e-7, 1e20, 1e21]
    for base in [1e-6, 1e21, 1.0, 9007199254740992.0, 2.2250738585072014e-308]:
        values.extend([math.nextafter(base, -math.inf), base, math.nextafter(base, math.inf)])
    for _ in range(10000):
        value = struct.unpack("d", rng.getrandbits(64).to_bytes(8, sys.byteorder))[0]
        if math.isfinite(value) and value != 0:
            values.append(value)
    raw = "".join(repr(v) + "\n" for v in values).encode()
    result = subprocess.run([exe, "number-batch"], input=raw, capture_output=True, check=True)
    actual = result.stdout.replace(b"\r\n", b"\n").decode().splitlines()
    expected = [canonicalize(v) for v in values]
    assert actual == expected, next((v, a, e) for v, a, e in zip(values, actual, expected) if a != e)
    node = shutil.which("node")
    if not node:
        raise AssertionError("Node is required for independent ECMAScript JCS conformance")
    reference = subprocess.run([node, "-e", "let s='';process.stdin.on('data',x=>s+=x);process.stdin.on('end',()=>process.stdout.write(s.trim().split('\\n').map(x=>JSON.stringify(JSON.parse(x))).join('\\n')+'\\n'))"],
                               input=raw, capture_output=True, check=True)
    assert result.stdout.replace(b"\r\n", b"\n") == reference.stdout
    for item in vectors["numberValid"]:
        expected = canonicalize(load_number_json(item["json"]))
        assert item["canonical"] == expected
        assert hashlib.sha256(expected.encode()).hexdigest() == item["sha256"]
    print(f"PASS {len(values)} binary64 values vs ECMAScript; Profile lexical policy, vectors and request identity")


if __name__ == "__main__":
    main(sys.argv[1])
