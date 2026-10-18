#!/usr/bin/env python3
"""Count target hot-loop instruction lines for the V2 three-operator experiment.

This is a static regression metric, not a cycle count. It follows the same
principle as Felucca tests/target_budget.py: count instructions inside compiled
per-sample loops. It additionally understands Xtensa hardware `loop` and bnez
backedges so the same script can summarize pi32v2 and ESP32-S3 disassemblies.

For fm6_op_run/bench_op_run_nointerp the C source has four specialized loops in
this stable order:
  0: modulated + add
  1: modulated + replace
  2: pure + add
  3: pure + replace
A generic 3-op cascade therefore costs pure_replace + 2*modulated_replace
instruction lines per sample, ignoring once-per-block call/setup overhead.
"""
from pathlib import Path
import csv
import re
import sys

if len(sys.argv) != 4:
    raise SystemExit("usage: analyze_hot_loops.py PLATFORM DISASM OUT_CSV")

platform, dis_path, out_path = sys.argv[1:]
lines = Path(dis_path).read_text(errors="replace").splitlines()

angle_label = re.compile(r"^\s*[0-9a-fA-F]+\s+<([^>]+)>:\s*$")
bare_label = re.compile(r"^([A-Za-z_.$][\w.$]*):\s*$")
insn_line = re.compile(r"^\s*([0-9a-fA-F]+):\s+.+$")

funcs = {}
cur = None
for line in lines:
    m = angle_label.match(line) or bare_label.match(line)
    if m:
        cur = m.group(1)
        funcs.setdefault(cur, [])
        continue
    if cur:
        m = insn_line.match(line)
        if m:
            funcs[cur].append((int(m.group(1), 16), line))


def loops(name):
    arr = funcs.get(name, [])
    found = []
    for i, (addr, line) in enumerate(arr):
        # Xtensa hardware loop: body starts at the next instruction and ends
        # immediately before the encoded loop-end address.
        if re.search(r"\bloop\b", line):
            m = re.search(r"\b([0-9a-fA-F]+)\s+<", line)
            if m and i + 1 < len(arr):
                end = int(m.group(1), 16)
                start = arr[i + 1][0]
                body = [(a, t) for a, t in arr if start <= a < end]
                if body:
                    found.append((start, body[-1][0], len(body), "hw-loop"))

        # Xtensa software loop backedge.
        if re.search(r"\bbnez(?:\.n)?\b", line):
            m = re.search(r"\b([0-9a-fA-F]+)\s+<", line)
            if m:
                start = int(m.group(1), 16)
                if start < addr:
                    body = [(a, t) for a, t in arr if start <= a <= addr]
                    found.append((start, addr, len(body), "backedge"))

        # JieLi objdump backedge format used by Felucca's own target_budget.py:
        #   if (...) goto -NN <func+... : 2a >
        if "goto -" in line:
            m = re.search(r"<[^>]*:\s*([0-9a-fA-F]+)\s*>", line)
            if m:
                start = int(m.group(1), 16)
                if start < addr:
                    body = [(a, t) for a, t in arr if start <= a <= addr]
                    found.append((start, addr, len(body), "backedge"))

    # De-duplicate and order by loop start address.
    uniq = {(s, e): (s, e, n, k) for s, e, n, k in found}
    return sorted(uniq.values())


op_i = loops("fm6_op_run")
op_n = loops("bench_op_run_nointerp")
fused_i = loops("bench_chain3_fused_interp")
fused_n = loops("bench_chain3_fused_nointerp")

if len(op_i) != 4 or len(op_n) != 4 or len(fused_i) != 1 or len(fused_n) != 1:
    raise SystemExit(
        "unexpected loop layout: "
        f"fm6_op_run={op_i}, nointerp={op_n}, fused_interp={fused_i}, fused_nointerp={fused_n}"
    )

interp_mod = op_i[1][2]
interp_pure = op_i[3][2]
nointerp_mod = op_n[1][2]
nointerp_pure = op_n[3][2]
generic_interp = interp_pure + 2 * interp_mod
generic_nointerp = nointerp_pure + 2 * nointerp_mod
fused_interp = fused_i[0][2]
fused_nointerp = fused_n[0][2]

rows = [
    (platform, "op_interp_mod_replace", interp_mod),
    (platform, "op_interp_pure_replace", interp_pure),
    (platform, "op_nointerp_mod_replace", nointerp_mod),
    (platform, "op_nointerp_pure_replace", nointerp_pure),
    (platform, "chain3_generic_interp", generic_interp),
    (platform, "chain3_generic_nointerp", generic_nointerp),
    (platform, "chain3_fused_interp", fused_interp),
    (platform, "chain3_fused_nointerp", fused_nointerp),
]

with open(out_path, "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["platform", "metric", "loop_instruction_lines_per_sample"])
    w.writerows(rows)

print(f"HOT-LOOP platform={platform} static_instruction_lines_not_cycles=1")
for _, metric, count in rows:
    print(f"{metric}={count}")
print(f"chain3_interp_fusion_ratio={generic_interp / fused_interp:.4f}")
print(f"chain3_nointerp_fusion_ratio={generic_nointerp / fused_nointerp:.4f}")
print(f"chain3_best_vs_generic_interp_ratio={generic_interp / min(fused_interp, fused_nointerp):.4f}")
