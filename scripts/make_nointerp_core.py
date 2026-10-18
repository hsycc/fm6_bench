#!/usr/bin/env python3
"""Generate a pinned Felucca FM6 core variant with operator sine interpolation removed.

The replacement uses a 1025-entry Q15 quarter-wave table and quadrant folding,
for 4096 discrete full-cycle phase positions. This is intentionally closer to
the stock FM-1 operator lookup than simply dropping interpolation from
Felucca's 1024-entry full-wave table.
"""
from pathlib import Path
import re
import sys

if len(sys.argv) != 3:
    raise SystemExit("usage: make_nointerp_core.py INPUT_FM6_CORE_C OUTPUT_C")

src_path = Path(sys.argv[1])
out_path = Path(sys.argv[2])
src = src_path.read_text()

pattern = re.compile(
    r"static inline int32_t fm6_sin\(int32_t ph\)\s*\{.*?\n\}",
    re.S,
)
replacement = """static inline int32_t fm6_sin(int32_t ph)\n{\n    /* Stock-like operator lookup experiment: 1024 steps/quadrant, no interpolation. */\n    uint32_t p = ((uint32_t)ph >> 12) & 4095u;\n    uint32_t q = p >> 10;\n    uint32_t x = p & 1023u;\n    if (q & 1u)\n        x = 1024u - x;\n    int32_t y = BENCH_QSINE4096[x];\n    return (q & 2u) ? -y : y;\n}"""

patched, n = pattern.subn(replacement, src, count=1)
if n != 1:
    raise SystemExit(f"expected exactly one fm6_sin() definition, replaced {n}")

patched = '#include "bench_qsine4096.h"\n' + patched
out_path.parent.mkdir(parents=True, exist_ok=True)
out_path.write_text(patched)
print(f"generated {out_path} from {src_path} (4096-phase quarter-wave no-interp)")
