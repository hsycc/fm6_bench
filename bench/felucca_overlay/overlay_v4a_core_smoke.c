/* Compile-only smoke for the AC7911B8 V4A core layered on the V3 core. */
#include <stdint.h>

static uint32_t fm1_ticks(void) { static uint32_t t; return ++t; }
static void fm1_irq_off(void) { __asm__ volatile("cli" ::: "memory"); }
static void fm1_irq_on(void) { __asm__ volatile("csync\n\tsti" ::: "memory"); }
static void fm1_wdt_feed(void) { }

#ifndef FELUCCA_CPU2
#define FELUCCA_CPU2 0
#endif
#include "felucca_tables.h"
#include "fm6_core.c"
#include "fm6_bench_ac_core.inc"
#include "fm6_v4a_ac_core.inc"

void fm6_v4a_core_smoke_entry(void)
{
    v4a_ac_start_case(8u);
    v4a_ac_half_begin();
    v4a_ac_audio_block(v4a_stereo_quantum);
    v4a_ac_half_end(V4A_AC_DEADLINE_TICKS / 2u, 0u);
    v4a_ac_finish_case(0u);
}
