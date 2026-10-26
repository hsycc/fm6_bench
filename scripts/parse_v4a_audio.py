#!/usr/bin/env python3
"""Validate one complete V4A capture; reject ambiguous or impossible results.

A pass is a short transport-baseline screening result, NOT product polyphony.
--source is operator-declared provenance; it is not hardware attestation.
"""
from __future__ import annotations
import argparse
import json
import math
import re
from pathlib import Path

EXPECTED = (1, 8, 12, 16, 20, 24)
PLATFORMS = ("ESP32-S3-I2S", "AC7911B8-Felucca-ALNK0")
NUMERIC = (
    "quanta", "samples", "deadline_ticks", "dsp_avg", "dsp_p95", "dsp_p99",
    "dsp_max", "dsp_miss", "write_avg", "write_p99", "write_max", "total_avg",
    "total_p99", "total_max", "total_miss", "starve", "write_fail", "dma_sent",
    "send_q_ovf",
)
ANSI = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")


def fields(text: str) -> dict[str, str]:
    result: dict[str, str] = {}
    for token in text.split():
        if "=" not in token:
            raise ValueError(f"malformed field: {token!r}")
        key, value = token.split("=", 1)
        if not key or not value or key in result:
            raise ValueError(f"empty or duplicate field: {key!r}")
        result[key] = value
    return result


def uint(data: dict, key: str, positive: bool = False) -> int:
    raw = data.get(key, "")
    if not re.fullmatch(r"[0-9]+", str(raw)):
        raise ValueError(f"missing/invalid unsigned decimal field {key}={raw!r}")
    value = int(raw)
    if value > 0xffffffff or (positive and not value):
        raise ValueError(f"out-of-range field {key}={value}")
    return value


def parse_capture(text: str) -> tuple[dict[str, str], list[dict[str, int]]]:
    header = None
    rows = []
    done = False
    for raw in text.splitlines():
        line = ANSI.sub("", raw).strip().lstrip("\ufeff")
        if line.startswith("FM6-V4A FAIL") or "Guru Meditation" in line or "panic'ed" in line:
            raise ValueError(f"device failure in capture: {line}")
        if line.startswith("FM6-V4A platform="):
            if header is not None or done or rows:
                raise ValueError("multiple/misordered sessions: capture exactly one run")
            header = fields(line[len("FM6-V4A "):])
        elif line.startswith("FM6-V4A voices="):
            if header is None or done:
                raise ValueError("result outside header/DONE boundaries")
            data = fields(line[len("FM6-V4A "):])
            voice = uint(data, "voices", True)
            if len(rows) >= len(EXPECTED) or voice != EXPECTED[len(rows)]:
                raise ValueError(f"duplicate, missing or out-of-order voice row: {voice}")
            if "deadline_ticks" not in data and "deadline_cycles" in data:
                data["deadline_ticks"] = data["deadline_cycles"]
            row = {key: uint(data, key) for key in NUMERIC}
            row["voices"] = voice
            row["warm_errors"] = uint(data, "warm_errors") if "warm_errors" in data else 0
            rows.append(row)
        elif line == "FM6-V4A DONE":
            if header is None or done or len(rows) != len(EXPECTED):
                raise ValueError("duplicate/premature DONE or missing voice rows")
            done = True
    if header is None or not done:
        raise ValueError("incomplete capture: need one header, all six rows and DONE")
    clock = uint(header, "clock_hz", True)
    nominal = uint(header, "dsp_nominal_sr", True)
    quantum = uint(header, "quantum_frames", True)
    if nominal != 44100 or quantum != 128:
        raise ValueError("not the V4A 44.1-kHz / 128-frame workload")
    if "ctl" in header and uint(header, "ctl", True) != 32:
        raise ValueError("V4A requires CTL=32")
    if ("transport_sr_num" in header) != ("transport_sr_div" in header):
        raise ValueError("incomplete transport clock ratio")
    rate_num = uint(header, "transport_sr_num", True) if "transport_sr_num" in header else nominal
    rate_div = uint(header, "transport_sr_div", True) if "transport_sr_div" in header else 1
    expected_deadline = (clock * quantum * rate_div + rate_num - 1) // rate_num
    quanta = None
    for row in rows:
        n = row["quanta"]
        if n < 100 or row["samples"] != n * quantum:
            raise ValueError("invalid measurement count or sample count")
        if quanta is not None and n != quanta:
            raise ValueError("measurement count changes within the run")
        quanta = n
        if row["deadline_ticks"] <= 0 or abs(row["deadline_ticks"] - expected_deadline) > 1:
            raise ValueError("deadline is zero or inconsistent with clock and transport")
        for name in ("dsp", "write", "total"):
            if not (0 <= row[name + "_avg"] <= row[name + "_max"] and
                    0 <= row[name + "_p99"] <= row[name + "_max"]):
                raise ValueError(f"inconsistent {name} statistics")
        if not 0 < row["dsp_p95"] <= row["dsp_p99"] <= row["dsp_max"]:
            raise ValueError("invalid DSP percentile ordering or zero timer")
        for name in ("dsp", "total"):
            missed = row[name + "_miss"]
            if missed > n or ((row[name + "_max"] > row["deadline_ticks"]) != (missed > 0)):
                raise ValueError(f"inconsistent {name} miss count / maximum")
    return header, rows


def evaluate(text: str, safe_percent: float = 85.0, source: str = "unspecified") -> dict:
    if not math.isfinite(safe_percent) or not 0 < safe_percent < 100:
        raise ValueError("safe-percent must be finite and strictly between 0 and 100")
    if source not in ("unspecified", "physical", "fixture", "host", "qemu"):
        raise ValueError("unknown source kind")
    header, rows = parse_capture(text)
    platform = header["platform"]
    if source == "physical" and platform not in PLATFORMS:
        raise ValueError("a fixture/host/unknown platform cannot be marked physical")
    safe_max = 0
    prefix_ok = True
    for row in rows:
        reasons = []
        util = row["dsp_p99"] * 100.0 / row["deadline_ticks"]
        if util > safe_percent:
            reasons.append("p99-margin")
        for key in ("dsp_miss", "starve", "write_fail", "send_q_ovf", "warm_errors"):
            if row[key]:
                reasons.append(key)
        # Two queued transport buffers allow a small snapshot-boundary delta.
        if not max(1, row["quanta"] - 2) <= row["dma_sent"] <= row["quanta"] + 2:
            reasons.append("dma-progress")
        # AC measures active ISR work. S3 total includes legitimate blocking
        # I2S wait, so total_miss on S3 is diagnostic, NOT DSP CPU utilization.
        if platform == "AC7911B8-Felucca-ALNK0" and row["total_miss"]:
            reasons.append("isr-total-miss")
        row["util_percent"] = util
        row["dsp_p99_us"] = row["dsp_p99"] * 1e6 / int(header["clock_hz"])
        row["safe"] = not reasons
        row["reasons"] = reasons
        prefix_ok = prefix_ok and row["safe"]
        if prefix_ok:
            safe_max = row["voices"]
    return dict(schema=1, source=source, provenance="operator-declared-not-attested",
                platform=platform, header=header, safe_percent=safe_percent,
                safe_max_tested_prefix=safe_max, product_polyphony_guaranteed=False,
                rows=rows)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("log", type=Path)
    ap.add_argument("--label", default="v4a")
    ap.add_argument("--source", choices=("unspecified", "physical", "fixture", "host", "qemu"), default="unspecified")
    ap.add_argument("--safe-percent", type=float, default=85.0)
    ap.add_argument("--json-out", type=Path)
    ap.add_argument("--require-voices", type=int, choices=(0, *EXPECTED), default=0)
    args = ap.parse_args()
    try:
        report = evaluate(args.log.read_text(encoding="utf-8", errors="replace"), args.safe_percent, args.source)
    except (ValueError, OSError) as exc:
        ap.error(str(exc))
    print(f"FM6-V4A-SUMMARY label={args.label} source={args.source} platform={report['platform']} safe_percent={args.safe_percent:g}")
    print("voices dsp_p99_us util_percent dma_sent safe reasons")
    for row in report["rows"]:
        print(f"{row['voices']:>6} {row['dsp_p99_us']:>10.3f} {row['util_percent']:>12.3f} "
              f"{row['dma_sent']:>8} {'yes' if row['safe'] else 'no':>4} {','.join(row['reasons']) or '-'}")
    value = report["safe_max_tested_prefix"]
    print(f"FM6-V4A-SAFE-MAX voices={value} safe_percent={args.safe_percent:g} source={args.source} scope=short-baseline-only")
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return 0 if value >= args.require_voices else 1


if __name__ == "__main__":
    raise SystemExit(main())
