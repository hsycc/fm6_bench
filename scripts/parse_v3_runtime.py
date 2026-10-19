#!/usr/bin/env python3
"""Parse FM6-V3 target logs and summarize real-time headroom.

Reads stdin or one/more files. QEMU results are functional only and should not
be treated as physical ESP32-S3 performance; pass --label accordingly.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

HEAD_RE = re.compile(
    r"FM6-V3 platform=(?P<platform>\S+) clock_hz=(?P<clock_hz>\d+) "
    r"cpu_hz=(?P<cpu_hz>\d+) sr=(?P<sr>\d+) block=(?P<block>\d+) samples=(?P<samples>\d+)"
)
ROW_RE = re.compile(r"FM6-V3 voices=(?P<voices>\d+) (?P<body>.*)")
KV_RE = re.compile(r"([A-Za-z0-9_]+)=([0-9a-fA-F]+)")


def read_text(paths: list[str]) -> str:
    if not paths:
        return sys.stdin.read()
    return "\n".join(Path(p).read_text(errors="replace") for p in paths)


def parse(text: str):
    head = None
    rows = []
    for line in text.splitlines():
        hm = HEAD_RE.search(line)
        if hm:
            head = {k: (v if k == "platform" else int(v)) for k, v in hm.groupdict().items()}
            continue
        rm = ROW_RE.search(line)
        if not rm:
            continue
        row = {"voices": int(rm.group("voices"))}
        for key, value in KV_RE.findall(rm.group("body")):
            row[key] = int(value, 16) if key == "sink" else int(value)
        rows.append(row)
    if head is None:
        raise SystemExit("no FM6-V3 header found")
    if not rows:
        raise SystemExit("no FM6-V3 result rows found")
    return head, rows


def ratio(x: int, d: int) -> float:
    return 100.0 * x / d if d else float("nan")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="*")
    ap.add_argument("--safe-percent", type=float, default=85.0,
                    help="p99 deadline utilization limit (default 85%%)")
    ap.add_argument("--label", default="target",
                    help="free-form provenance label, e.g. real-s3 or qemu-functional")
    args = ap.parse_args()

    head, rows = parse(read_text(args.files))
    rows.sort(key=lambda r: r["voices"])
    clock_hz = head["clock_hz"]
    cpu_hz = head["cpu_hz"]
    block = head["block"]

    print(f"FM6-V3-SUMMARY label={args.label} platform={head['platform']} "
          f"clock_hz={clock_hz} cpu_hz={cpu_hz} sr={head['sr']} block={block} "
          f"safe_p99_percent={args.safe_percent:.1f}")
    print("voices avg_ticks p99_ticks max_ticks avg_ticks/sample p99_util% max_util% misses avg_cycles/sample p99_cycles/sample verdict")

    safe_max = 0
    for r in rows:
        deadline = r.get("deadline", r.get("deadline_ticks", 0))
        avg = r.get("avg", r.get("avg_ticks", 0))
        p99 = r.get("p99", r.get("p99_ticks", 0))
        mx = r.get("max", r.get("max_ticks", 0))
        misses = r.get("miss", r.get("misses", 0))
        avg_per_sample = avg / block
        p99_util = ratio(p99, deadline)
        max_util = ratio(mx, deadline)
        if cpu_hz:
            avg_cps = avg * cpu_hz / clock_hz / block
            p99_cps = p99 * cpu_hz / clock_hz / block
            avg_cps_s = f"{avg_cps:.1f}"
            p99_cps_s = f"{p99_cps:.1f}"
        else:
            avg_cps_s = p99_cps_s = "-"
        ok = misses == 0 and p99_util <= args.safe_percent
        if ok:
            safe_max = max(safe_max, r["voices"])
        verdict = "PASS" if ok else "FAIL"
        print(f"{r['voices']:>6} {avg:>9} {p99:>9} {mx:>9} {avg_per_sample:>16.2f} "
              f"{p99_util:>9.2f} {max_util:>9.2f} {misses:>6} {avg_cps_s:>17} "
              f"{p99_cps_s:>17} {verdict}")

    print(f"FM6-V3-SAFE-MAX voices={safe_max} criterion=p99<={args.safe_percent:.1f}%_deadline_and_zero_misses")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
