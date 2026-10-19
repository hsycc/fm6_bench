#!/usr/bin/env python3
"""Apply V3 plus the V4A real-ALNK0 FM6 benchmark to pinned Felucca.

V4A leaves Felucca's normal audio path unchanged when inactive. While the CDC
`fm6audiobench` command is active, the real ALNK0 DMA ISR renders the shared
FM6 benchmark workload into each 128-frame half and records synthesis/mix,
DMA-buffer copy, whole-ISR deadline, and real DMA-late events.
"""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path


def insert_once(text: str, needle: str, addition: str, label: str) -> str:
    if addition.strip() in text:
        return text
    if needle not in text:
        raise SystemExit(f"cannot patch {label}: anchor not found: {needle!r}")
    return text.replace(needle, needle + addition, 1)


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if new in text:
        return text
    if old not in text:
        raise SystemExit(f"cannot patch {label}: replacement anchor not found")
    return text.replace(old, new, 1)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("felucca", help="path to Felucca checkout")
    args = ap.parse_args()

    repo = Path(__file__).resolve().parents[1]
    felucca = Path(args.felucca).resolve()
    src = felucca / "firmware" / "src"
    if not (src / "felucca.c").exists() or not (src / "audio.c").exists():
        raise SystemExit(f"not a Felucca checkout: {felucca}")

    # V4A reuses V3's exact benchmark FM6 state and console formatting helpers.
    # Do not blindly rerun the V3 patcher on an already-V4A tree: V4A extends
    # the V3 help/dispatch text, so V3's original textual anchors no longer
    # exist. Detect the stable semantic V3 markers instead.
    felucca_before = (src / "felucca.c").read_text()
    console_before = (src / "console.c").read_text()
    have_v3 = ('#include "fm6_bench_ac_core.inc"' in felucca_before and
               'con_word(&p, "fm6bench")' in console_before and
               (src / "fm6_bench_console.inc").exists())
    if not have_v3:
        subprocess.run([sys.executable, str(repo / "scripts" / "apply_felucca_v3_overlay.py"),
                        str(felucca)], check=True)

    bench_dst = src / "fm6_bench"
    shutil.copy2(repo / "bench" / "v4a_audio_workload.inc",
                 bench_dst / "v4a_audio_workload.inc")
    shutil.copy2(repo / "bench" / "felucca_overlay" / "fm6_v4a_ac_core.inc",
                 src / "fm6_v4a_ac_core.inc")
    shutil.copy2(repo / "bench" / "felucca_overlay" / "fm6_v4a_console.inc",
                 src / "fm6_v4a_console.inc")

    # V3 core is already immediately after engines.c. V4A must also be before
    # audio.c so the real ISR can call its static hooks in the unity TU.
    felucca_c = src / "felucca.c"
    text = felucca_c.read_text()
    v3_inc = '#include "fm6_bench_ac_core.inc"  /* fm6_bench V3 overlay */'
    v4_inc = '\n#include "fm6_v4a_ac_core.inc"  /* fm6_bench V4A overlay */'
    text = insert_once(text, v3_inc, v4_inc, "felucca.c")
    felucca_c.write_text(text)

    # Route only active benchmark halves through the shared workload. The
    # inactive branch preserves Felucca's original shedding/load/late ordering.
    audio_c = src / "audio.c"
    text = audio_c.read_text()
    old_half = '''    if (p & FM1_AUDIO_HALF) {
        uint32_t half = fm1_audio_free_half(), b, us;
        int32_t *o = &abuf[half * HALF_WORDS];
        if (shed_req) {
            shed_req = 0;
            shed_voice();
        }
#if FELUCCA_UAC
        uac_render_start();
#endif
        for (b = 0; b < HALF_FRAMES; b += CTL) {
#ifdef FM1_INPUT_LAT
            kb_out_tick = t0 + (HALF_FRAMES + b) * DAC_TICKS;   /* when this block plays (seq.c kb_lat) */
#endif
            audio_block(o + 2u * b, CTL);
        }
        fm1_audio_ack_half();
        audio_halves++;
        us = shed_check(fm1_ticks() - t0);
        if (us > audio_max_us)
            audio_max_us = us;
        {
            uint32_t load = song.cpu_q8 * (CPU_AVG - 1u) + (us * 256u) / (HALF_FRAMES * 1000000u / FS) + audio_cpu_rem;
            song.cpu_q8 = load / CPU_AVG;
            audio_cpu_rem = load % CPU_AVG;
        }
        if (fm1_audio_free_half() != half)
            felucca_dbg.late++;                         /* the DMA moved on while we rendered */
        felucca_dbg.halves++;
        felucca_dbg.last_us = us;
        if (us > felucca_dbg.max_us)
            felucca_dbg.max_us = us;
        felucca_dbg.cpu_q8 = song.cpu_q8;
    }
'''
    new_half = '''    if (p & FM1_AUDIO_HALF) {
        uint32_t half = fm1_audio_free_half(), b, us;
        uint8_t v4a = v4a_ac_active;                    /* latch one mode for this complete DMA half */
        int32_t *o = &abuf[half * HALF_WORDS];
        if (!v4a && shed_req) {
            shed_req = 0;
            shed_voice();
        }
#if FELUCCA_UAC
        if (!v4a)
            uac_render_start();
#endif
        if (v4a)
            v4a_ac_half_begin();
        for (b = 0; b < HALF_FRAMES; b += CTL) {
#ifdef FM1_INPUT_LAT
            kb_out_tick = t0 + (HALF_FRAMES + b) * DAC_TICKS;   /* when this block plays (seq.c kb_lat) */
#endif
            if (v4a)
                v4a_ac_audio_block(o + 2u * b);
            else
                audio_block(o + 2u * b, CTL);
        }
        fm1_audio_ack_half();
        audio_halves++;
        if (v4a) {
            uint32_t all = fm1_ticks() - t0;
            uint32_t late = fm1_audio_free_half() != half;
            v4a_ac_half_end(all, late);
        } else {
            us = shed_check(fm1_ticks() - t0);
            if (us > audio_max_us)
                audio_max_us = us;
            {
                uint32_t load = song.cpu_q8 * (CPU_AVG - 1u) + (us * 256u) / (HALF_FRAMES * 1000000u / FS) + audio_cpu_rem;
                song.cpu_q8 = load / CPU_AVG;
                audio_cpu_rem = load % CPU_AVG;
            }
            if (fm1_audio_free_half() != half)
                felucca_dbg.late++;                     /* the DMA moved on while we rendered */
            felucca_dbg.halves++;
            felucca_dbg.last_us = us;
            if (us > felucca_dbg.max_us)
                felucca_dbg.max_us = us;
            felucca_dbg.cpu_q8 = song.cpu_q8;
        }
    }
'''
    text = replace_once(text, old_half, new_half, "audio.c")
    audio_c.write_text(text)

    console_c = src / "console.c"
    text = console_c.read_text()
    v3_console_inc = '#include "fm6_bench_console.inc"  /* fm6_bench V3 overlay */'
    v4_console_inc = '\n#include "fm6_v4a_console.inc"  /* fm6_bench V4A overlay */'
    text = insert_once(text, v3_console_inc, v4_console_inc, "console.c")

    old_help = 'con_puts("status  fm6bench  dbg  inp  crash  params  memr ADDR [LEN]  flr OFF [LEN]  uboot yes\\r\\n");'
    new_help = 'con_puts("status  fm6bench  fm6audiobench  dbg  inp  crash  params  memr ADDR [LEN]  flr OFF [LEN]  uboot yes\\r\\n");'
    text = replace_once(text, old_help, new_help, "console.c help")

    old_dispatch = ('    else if (con_word(&p, "fm6bench"))\n'
                    '        con_fm6bench();\n')
    v4_dispatch = ('    else if (con_word(&p, "fm6audiobench"))\n'
                   '        con_fm6audiobench();\n')
    text = insert_once(text, old_dispatch, v4_dispatch, "console.c dispatch")
    console_c.write_text(text)

    print(f"FM6 V4A Felucca overlay applied to {felucca}")
    print("CDC commands: fm6bench (V3 raw DSP), fm6audiobench (V4A real ALNK0)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
