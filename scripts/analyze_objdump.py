#!/usr/bin/env python3
import csv
import re
import sys
from pathlib import Path

if len(sys.argv) != 4:
    raise SystemExit("usage: analyze_objdump.py PLATFORM DISASM OUT_CSV")

platform, dis_path, out_path = sys.argv[1:]
text = Path(dis_path).read_text(errors="replace").splitlines()

wanted = (
    "fm6_op_run",
    "fm6_op_fb",
    "fm6_core_render",
    "fm6_note_compute",
    "bench_render_voices",
    "bench_target_entry",
)

current = None
counts = {k: 0 for k in wanted}
label_re = re.compile(r"^\s*[0-9a-fA-F]+\s+<([^>]+)>:\s*$")
insn_re = re.compile(r"^\s*[0-9a-fA-F]+:\s+([0-9a-fA-F]{2,8}(?:\s+[0-9a-fA-F]{2,8})*)\s+\S+")

def canonical(symbol: str):
    for name in wanted:
        if symbol == name or symbol.startswith(name + "."):
            return name
    return None

for line in text:
    m = label_re.match(line)
    if m:
        current = canonical(m.group(1))
        continue
    if current and insn_re.match(line):
        counts[current] += 1

rows = [(platform, name, counts[name]) for name in wanted]
with open(out_path, "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["platform", "function", "instructions"])
    w.writerows(rows)

print(f"TARGET-ASM platform={platform}")
for _, name, count in rows:
    print(f"{name} instructions={count}")

required = ("fm6_op_run", "fm6_op_fb", "fm6_note_compute", "bench_render_voices")
missing = [name for name in required if counts[name] == 0]
if missing:
    raise SystemExit("failed to identify required function disassembly: " + ", ".join(missing))
