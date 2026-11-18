"""Check ABox firmware sources for integer formats unsafe on newlib-nano."""

import argparse
import os
from pathlib import Path
import re


PLATFORM = Path(__file__).resolve().parents[1]
SOURCE_SUFFIXES = {".c", ".h", ".cc", ".cpp", ".hpp"}
EXCLUDED_DIRS = {".git", "build", "cmakefiles", "dist", "out", "drivers", "vendor",
                 "third_party", "third-party", "tests"}
COMMENTS_AND_LITERALS = re.compile(
    r'(?P<string>"(?:\\.|[^"\\])*")|\'(?:\\.|[^\'\\])*\'|(?P<comment>//[^\n]*|/\*.*?\*/)',
    re.DOTALL,
)
PRINTF_INTEGER = re.compile(r"%%|%(?:\d+\$)?[-+#0-9.* ']*ll[diuoxX]")
PRINTF_INTEGER_MACRO = re.compile(r"\s*PRI[diuoxX]64\b")
JSON_NUMBER = re.compile(
    r'\bcJSON_AddNumberToObject\s*\([^,;]+,\s*"(?:timestamp|uptimeMs)"', re.IGNORECASE
)


def sources(root):
    for folder, dirs, files in os.walk(root):
        dirs[:] = sorted(name for name in dirs if name.lower() not in EXCLUDED_DIRS
                         and not name.lower().startswith(("build-", "cmake-build-")))
        for name in sorted(files):
            path = Path(folder, name)
            if path.suffix.lower() in SOURCE_SUFFIXES:
                yield path


def findings(path):
    source = path.read_text(encoding="utf-8", errors="surrogateescape")
    # Preserve offsets so diagnostics point to the original source line.
    source = COMMENTS_AND_LITERALS.sub(
        lambda match: re.sub(r"[^\n]", " ", match[0]) if match["comment"] else match[0], source
    )
    for literal in COMMENTS_AND_LITERALS.finditer(source):
        if literal["string"] is None:
            continue
        if (any(spec[0] != "%%" for spec in PRINTF_INTEGER.finditer(literal[0])) or
                (literal[0][:-1].endswith("%") and PRINTF_INTEGER_MACRO.match(source, literal.end()))):
            yield source.count("\n", 0, literal.start()) + 1, (
                "format 64-bit integers with ABoxMqttV4_FormatInteger or the response encoder"
            )
    for match in JSON_NUMBER.finditer(source):
        yield source.count("\n", 0, match.start()) + 1, (
            "encode timestamp/uptimeMs as decimal text with cJSON_AddRawToObject"
        )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--product-root", type=Path, action="append", default=[],
                        help="also scan this product's app, shared and platform configuration sources")
    args = parser.parse_args()
    roots = [PLATFORM / name for name in ("components", "hardware", "targets", "examples")]
    for product in args.product_root:
        product = product.resolve()
        app = product / "app"
        if not app.is_dir():
            parser.error(f"product app directory not found: {app}")
        roots.append(app)
        roots.extend(root for root in (product / "shared", product / "platform_config") if root.is_dir())
    failed = False
    for root in roots:
        for path in sources(root):
            for line, reason in findings(path):
                print(f"{path}:{line}: {reason}")
                failed = True
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
