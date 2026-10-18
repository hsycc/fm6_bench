#!/usr/bin/env python3
"""Generate a pinned Felucca FM6 core variant with operator sine interpolation removed.

This intentionally changes only fm6_sin(). Everything else remains byte-for-byte
from the pinned upstream source, so full-engine benchmarks isolate the cost and
output effect of operator sine interpolation.
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
replacement = """static inline int32_t fm6_sin(int32_t ph)\n{\n    /* FM-1-style operator lookup experiment: nearest LUT sample, no interpolation. */\n    return SINE[((uint32_t)ph >> 14) & 1023u];\n}"""

patched, n = pattern.subn(replacement, src, count=1)
if n != 1:
    raise SystemExit(f"expected exactly one fm6_sin() definition, replaced {n}")

out_path.parent.mkdir(parents=True, exist_ok=True)
out_path.write_text(patched)
print(f"generated {out_path} from {src_path}")
