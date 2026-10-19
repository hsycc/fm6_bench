#!/usr/bin/env python3
"""Apply the FM6 V3 runtime benchmark as a small overlay to pinned Felucca.

The upstream checkout remains the source of the synth/USB/board support. This
script copies only the benchmark harness into firmware/src/fm6_bench and adds:
  * one include after engines.c so the benchmark reuses Felucca's exact FM6 core;
  * one include + `fm6bench` dispatch in the CDC diagnostics console.

Run on a clean Felucca checkout pinned to FELUCCA_SHA. The operation is
idempotent; an already-patched tree is left intact.
"""
from __future__ import annotations

import argparse
import shutil
from pathlib import Path


def insert_once(text: str, needle: str, addition: str, label: str) -> str:
    if addition.strip() in text:
        return text
    if needle not in text:
        raise SystemExit(f"cannot patch {label}: anchor not found: {needle!r}")
    return text.replace(needle, needle + addition, 1)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("felucca", help="path to Felucca checkout")
    args = ap.parse_args()

    repo = Path(__file__).resolve().parents[1]
    felucca = Path(args.felucca).resolve()
    src = felucca / "firmware" / "src"
    if not (src / "felucca.c").exists() or not (src / "console.c").exists():
        raise SystemExit(f"not a Felucca checkout: {felucca}")

    dst = src / "fm6_bench"
    dst.mkdir(exist_ok=True)
    for name in ("fm6_workload.inc", "target_runtime_core.inc", "target_runtime_result.h"):
        shutil.copy2(repo / "bench" / name, dst / name)
    shutil.copy2(repo / "bench" / "felucca_overlay" / "fm6_bench_ac_core.inc",
                 src / "fm6_bench_ac_core.inc")
    shutil.copy2(repo / "bench" / "felucca_overlay" / "fm6_bench_console.inc",
                 src / "fm6_bench_console.inc")

    felucca_c = src / "felucca.c"
    text = felucca_c.read_text()
    text = insert_once(text, '#include "engines.c"',
                       '\n#include "fm6_bench_ac_core.inc"  /* fm6_bench V3 overlay */',
                       "felucca.c")
    felucca_c.write_text(text)

    console_c = src / "console.c"
    text = console_c.read_text()
    if '#include "fm6_bench_console.inc"' not in text:
        anchor = "static void con_exec(const char *p)\n{"
        if anchor not in text:
            raise SystemExit("cannot patch console.c: con_exec anchor not found")
        text = text.replace(anchor,
                            '#include "fm6_bench_console.inc"  /* fm6_bench V3 overlay */\n\n' + anchor,
                            1)

    old_help = 'con_puts("status  dbg  inp  crash  params  memr ADDR [LEN]  flr OFF [LEN]  uboot yes\\r\\n");'
    new_help = 'con_puts("status  fm6bench  dbg  inp  crash  params  memr ADDR [LEN]  flr OFF [LEN]  uboot yes\\r\\n");'
    if new_help not in text:
        if old_help not in text:
            raise SystemExit("cannot patch console.c: help anchor not found")
        text = text.replace(old_help, new_help, 1)

    old_dispatch = '    else if (con_word(&p, "status"))\n        con_status();\n'
    new_dispatch = (old_dispatch +
                    '    else if (con_word(&p, "fm6bench"))\n'
                    '        con_fm6bench();\n')
    if 'con_word(&p, "fm6bench")' not in text:
        if old_dispatch not in text:
            raise SystemExit("cannot patch console.c: status dispatch anchor not found")
        text = text.replace(old_dispatch, new_dispatch, 1)
    console_c.write_text(text)

    print(f"FM6 V3 Felucca overlay applied to {felucca}")
    print("CDC command: fm6bench")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
