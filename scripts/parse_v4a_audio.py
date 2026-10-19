#!/usr/bin/env python3
"""Parse FM6-V4A full-audio logs and apply the transport-aware safety gate."""
from __future__ import annotations

import argparse
import re
from pathlib import Path

EXPECTED = (1, 8, 12, 16, 20, 24)
ROW = re.compile(r"^FM6-V4A voices=(\d+)\s+(.*)$")


def fields(text: str) -> dict[str, int]:
    out: dict[str, int] = {}
    for tok in text.split():
        if "=" not in tok:
            continue
        k, v = tok.split("=", 1)
        try:
            out[k] = int(v, 0)
        except ValueError:
            pass
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("log", type=Path)
    ap.add_argument("--label", default="v4a")
    ap.add_argument("--safe-percent", type=float, default=85.0)
    args = ap.parse_args()

    text = args.log.read_text(errors="replace")
    if "FM6-V4A platform=" not in text:
        raise SystemExit("missing FM6-V4A platform header")
    if "FM6-V4A DONE" not in text:
        raise SystemExit("incomplete capture: missing FM6-V4A DONE")

    rows: dict[int, dict[str, int]] = {}
    for line in text.splitlines():
        m = ROW.match(line.strip())
        if m:
            rows[int(m.group(1))] = fields(m.group(2))

    missing = [v for v in EXPECTED if v not in rows]
    if missing:
        raise SystemExit(f"missing V4A voice rows: {missing}")

    print(f"FM6-V4A-SUMMARY label={args.label} safe_percent={args.safe_percent:g}")
    print("voices dsp_p99 util% dsp_miss starve write_fail qovf write_p99 total_p99 safe")
    safe_max = 0
    for voices in EXPECTED:
        r = rows[voices]
        required = (
            "deadline_cycles", "dsp_p99", "dsp_miss", "starve", "write_fail",
            "send_q_ovf", "write_p99", "total_p99"
        )
        absent = [k for k in required if k not in r]
        if absent:
            raise SystemExit(f"voices={voices}: missing fields {absent}")
        deadline = r["deadline_cycles"]
        util = (100.0 * r["dsp_p99"] / deadline) if deadline else 0.0
        safe = (
            util <= args.safe_percent
            and r["dsp_miss"] == 0
            and r["starve"] == 0
            and r["write_fail"] == 0
            and r["send_q_ovf"] == 0
        )
        if safe:
            safe_max = voices
        print(
            f"{voices:>6} {r['dsp_p99']:>7} {util:>5.1f} {r['dsp_miss']:>8} "
            f"{r['starve']:>6} {r['write_fail']:>10} {r['send_q_ovf']:>4} "
            f"{r['write_p99']:>9} {r['total_p99']:>9} {'yes' if safe else 'no'}"
        )

    print(f"FM6-V4A-SAFE-MAX voices={safe_max} safe_percent={args.safe_percent:g}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
