#!/usr/bin/env python3
import csv
import re
import sys
from pathlib import Path

if len(sys.argv) != 5:
    raise SystemExit("usage: analyze_objdump.py PLATFORM DISASM SYMBOLS OUT_CSV")

platform, dis_path, sym_path, out_path = sys.argv[1:]
dis_lines = Path(dis_path).read_text(errors="replace").splitlines()
sym_lines = Path(sym_path).read_text(errors="replace").splitlines()

wanted = (
    "fm6_op_run",
    "fm6_op_fb",
    "fm6_core_render",
    "fm6_note_compute",
    "bench_render_voices",
    "bench_target_entry",
)

counts = {k: 0 for k in wanted}
sizes = {k: 0 for k in wanted}

# GNU objdump: 00000000 <symbol>:
angle_label_re = re.compile(r"^\s*[0-9a-fA-F]+\s+<([^>]+)>:\s*$")
# JieLi objdump: symbol:
bare_label_re = re.compile(r"^([A-Za-z_.$][\w.$]*):\s*$")
# Address, encoded bytes/word(s), mnemonic.
insn_re = re.compile(r"^\s*[0-9a-fA-F]+:\s+(?:[0-9a-fA-F]{2,8}(?:\s+[0-9a-fA-F]{2,8})*)\s+\S+")
hex_re = re.compile(r"^[0-9a-fA-F]+$")

def canonical(symbol: str):
    for name in wanted:
        if symbol == name or symbol.startswith(name + ".") or symbol.startswith(name + "$"):
            return name
    return None

current = None
for line in dis_lines:
    m = angle_label_re.match(line) or bare_label_re.match(line)
    if m:
        current = canonical(m.group(1))
        continue
    if current and insn_re.match(line):
        counts[current] += 1

# Works with both `nm -S` and JieLi `objdump -t`: symbol is final token,
# size is the preceding hex token in both formats used by this workflow.
for line in sym_lines:
    fields = line.split()
    if len(fields) < 3:
        continue
    name = canonical(fields[-1])
    if not name:
        continue
    size_token = fields[-2]
    if hex_re.match(size_token):
        sizes[name] = max(sizes[name], int(size_token, 16))

rows = [(platform, name, counts[name], sizes[name]) for name in wanted]
with open(out_path, "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["platform", "function", "instructions", "code_bytes"])
    w.writerows(rows)

print(f"TARGET-ASM platform={platform}")
for _, name, count, size in rows:
    inline = " (inlined)" if count == 0 and size == 0 else ""
    print(f"{name} instructions={count} code_bytes={size}{inline}")

# note_compute/core_render are expected to be inlined at -Os on both current compilers.
required = ("fm6_op_run", "fm6_op_fb", "bench_render_voices", "bench_target_entry")
missing = [name for name in required if counts[name] == 0 or sizes[name] == 0]
if missing:
    raise SystemExit("failed to identify required target functions: " + ", ".join(missing))
