#!/usr/bin/env python3
import math
import re
import struct
import sys
from pathlib import Path

if len(sys.argv) != 5:
    raise SystemExit("usage: compare_full_engine.py BASE_LOG NOINTERP_LOG BASE_PCM NOINTERP_PCM")

base_log, no_log, base_pcm, no_pcm = map(Path, sys.argv[1:])

line_re = re.compile(r"^voices=(\d+)\s+avg_ns=(\d+).*crc32=([0-9a-f]+)")

def parse_log(path):
    out = {}
    for line in path.read_text().splitlines():
        m = line_re.match(line)
        if m:
            out[int(m.group(1))] = (int(m.group(2)), m.group(3))
    if not out:
        raise SystemExit(f"no benchmark rows found in {path}")
    return out

base = parse_log(base_log)
no = parse_log(no_log)
if set(base) != set(no):
    raise SystemExit("voice sets differ between baseline and nointerp logs")

print("FULL-ENGINE-TIMING")
for voices in sorted(base):
    b_ns, b_crc = base[voices]
    n_ns, n_crc = no[voices]
    print(
        f"voices={voices} baseline_ns={b_ns} nointerp_ns={n_ns} "
        f"speedup={b_ns / n_ns:.4f} baseline_crc={b_crc} nointerp_crc={n_crc}"
    )

bb = base_pcm.read_bytes()
nb = no_pcm.read_bytes()
if len(bb) != len(nb) or len(bb) % 4:
    raise SystemExit(f"PCM size mismatch/invalid: baseline={len(bb)} nointerp={len(nb)}")

signal_sq = 0
error_sq = 0
abs_sum = 0
max_abs = 0
n = len(bb) // 4
for (a,), (b,) in zip(struct.iter_unpack("<i", bb), struct.iter_unpack("<i", nb)):
    e = b - a
    ae = abs(e)
    signal_sq += a * a
    error_sq += e * e
    abs_sum += ae
    max_abs = max(max_abs, ae)

snr = 999.0 if error_sq == 0 else 10.0 * math.log10(signal_sq / error_sq)
rms_signal = math.sqrt(signal_sq / n)
rms_error = math.sqrt(error_sq / n)
mean_abs = abs_sum / n
print(
    f"FULL-ENGINE-FIDELITY samples={n} snr_db={snr:.3f} "
    f"rms_signal={rms_signal:.3f} rms_error={rms_error:.3f} "
    f"mean_abs={mean_abs:.3f} max_abs={max_abs}"
)
