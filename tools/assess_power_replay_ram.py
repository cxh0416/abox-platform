"""Read-only RAM gate for a straightforward four-record dual-slot journal.

This is a layout estimate, not a journal implementation or proof that all
possible streaming storage designs are impossible. It preserves the existing
mailbox and heap/stack guards and never writes an App build or firmware image.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def symbols(nm, path):
    result = subprocess.run([nm, "-S", str(path)], check=True, capture_output=True, text=True)
    addresses, sizes = {}, {}
    for line in result.stdout.splitlines():
        fields = line.split()
        if len(fields) >= 3 and re.fullmatch(r"[0-9a-fA-F]+", fields[0]):
            addresses[fields[-1]] = int(fields[0], 16)
            if len(fields) == 4:
                sizes[fields[-1]] = int(fields[1], 16)
    return addresses, sizes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, action="append", required=True)
    args = parser.parse_args()
    cc, nm = shutil.which("arm-none-eabi-gcc"), shutil.which("arm-none-eabi-nm")
    if not cc or not nm:
        raise SystemExit("ARM compiler and nm are required for measured sizeof/layout evidence")
    work = ROOT / "build/host/power-replay-ram"
    work.mkdir(parents=True, exist_ok=True)
    source, obj = work / "layout.c", work / "layout.o"
    # Current power responses fit 288 bytes. This is NOT a new wire limit.
    source.write_text('''#include "abox_ec800_ufs.h"
typedef struct {
    char request_id[65]; unsigned char digest[32]; uint64_t expires_at;
    uint16_t response_length; char response[288]; uint8_t state;
} Record;
typedef struct {
    uint32_t magic, version, generation; Record records[4]; uint32_t crc;
} Snapshot;
unsigned char ufs_context_bytes[sizeof(ABoxEc800Ufs)];
unsigned char record_bytes[sizeof(Record)];
unsigned char snapshot_bytes[sizeof(Snapshot)];
''', encoding="utf-8", newline="\n")
    subprocess.run([cc, "-mcpu=cortex-m3", "-mthumb", "-c", str(source), "-o", str(obj),
                    "-I" + str(ROOT / "components/ec800/include")], check=True)
    _, layout = symbols(nm, obj)
    estimate = 2 * layout["snapshot_bytes"] + layout["ufs_context_bytes"]
    reports = []
    for elf in args.elf:
        addresses, sizes = symbols(nm, elf)
        bss_room = 0x2000BC40 - addresses["_ebss"]
        guard_room = addresses["_sstack"] - addresses["__ram_reserved_end"]
        credit = sizes.get("g_power_replay", 0)
        reports.append({"elf": str(elf.resolve()), "bss_before_mailbox_bytes": bss_room,
                        "reserved_end_to_stack_guard_bytes": guard_room,
                        "arm_ufs_context_bytes": layout["ufs_context_bytes"],
                        "arm_record_bytes": layout["record_bytes"],
                        "arm_snapshot_bytes": layout["snapshot_bytes"],
                        "two_snapshots_plus_ufs_bytes": estimate,
                        "credited_existing_power_cache_bytes": credit,
                        "estimated_net_bytes": estimate - credit,
                        "gate": "PASS" if estimate - credit <= min(bss_room, guard_room) else "STOP",
                        "not_measured": ["UFS commit latency", "physical power-cut recovery", "write endurance"]})
    print(json.dumps(reports, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
