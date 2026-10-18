#!/usr/bin/env python3
import csv
import re
import sys
from pathlib import Path

if len(sys.argv) != 4:
    raise SystemExit("usage: analyze_objdump.py PLATFORM DISASM OUT_CSV")

platform, dis_path, out_path = sys.argv[1:]
text = Path(dis_path).read_text(errors="replace").splitlines()

wanted = {
    "fm6_op_run",
    "fm6_op_fb",
    "fm6_core_render",
    "fm6_note_compute",
    "bench_render_voices",
    "bench_target_entry",
}

sym = None
counts = {k: 0 for k in wanted}
# GNU objdump labels look like: 00000000 <fm6_op_run>:
label_re = re.compile(r"^\s*[0-9a-fA-F]+\s+<([^>]+)>:\s*$")
# Require address + at least one encoded byte/word field + mnemonic text.
insn_re = re.compile(r"^\s*[0-9a-fA-F]+:\s+([0-9a-fA-F]{2,8}(?:\s+[0-9a-fA-F]{2,8})*)\s+\S+")

for line in text:
    m = label_re.match(line)
    if m:
        sym = m.group(1)
        continue
    if sym in wanted and insn_re.match(line):
        counts[sym] += 1

rows = [(platform, name, counts[name]) for name in sorted(wanted)]
with open(out_path, "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["platform", "function", "instructions"])
    w.writerows(rows)

print(f"TARGET-ASM platform={platform}")
for _, name, count in rows:
    print(f"{name} instructions={count}")

if counts["fm6_op_run"] == 0 or counts["fm6_note_compute"] == 0:
    raise SystemExit("failed to identify required function disassembly")
