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
    "bench_op_run_nointerp",
    "bench_chain3_generic_interp",
    "bench_chain3_generic_nointerp",
    "bench_chain3_fused_interp",
    "bench_chain3_fused_nointerp",
    "bench_target_opt_entry",
)

counts = {k: 0 for k in wanted}
sizes = {k: 0 for k in wanted}

angle_label_re = re.compile(r"^\s*[0-9a-fA-F]+\s+<([^>]+)>:\s*$")
bare_label_re = re.compile(r"^([A-Za-z_.$][\w.$]*):\s*$")
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

# Symbol formats differ:
# GNU nm -S:     ADDRESS SIZE TYPE NAME
# JieLi objdump: ADDRESS ... SECTION SIZE NAME
# In both, the last hex token before NAME is the function size.
for line in sym_lines:
    fields = line.split()
    if len(fields) < 3:
        continue
    name = canonical(fields[-1])
    if not name:
        continue
    hex_fields = [f for f in fields[:-1] if hex_re.match(f)]
    if len(hex_fields) >= 2:
        sizes[name] = max(sizes[name], int(hex_fields[-1], 16))

rows = [(platform, name, counts[name], sizes[name]) for name in wanted]
with open(out_path, "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["platform", "function", "instructions", "code_bytes"])
    w.writerows(rows)

print(f"TARGET-ASM platform={platform}")
for _, name, count, size in rows:
    if count == 0 and size == 0:
        note = " (inlined/removed)"
    elif count == 0 and size > 0:
        # GCC/Clang may emit two symbols at one address and objdump prints only
        # one label. The symbol table still proves the other function exists.
        note = " (aliased/label-folded)"
    else:
        note = ""
    print(f"{name} instructions={count} code_bytes={size}{note}")

required = (
    "fm6_op_run",
    "fm6_op_fb",
    "bench_render_voices",
    "bench_target_entry",
    "bench_op_run_nointerp",
    "bench_chain3_generic_interp",
    "bench_chain3_generic_nointerp",
    "bench_chain3_fused_interp",
    "bench_chain3_fused_nointerp",
    "bench_target_opt_entry",
)
# A non-zero symbol size with no separate disassembly label is valid: modern
# compilers can merge identical functions and leave multiple symbols pointing
# at one body. Fail only when both disassembly and symbol table lost a function.
missing = [name for name in required if counts[name] == 0 and sizes[name] == 0]
if missing:
    raise SystemExit("failed to identify required target functions: " + ", ".join(missing))
