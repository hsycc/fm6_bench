/* Compile-only smoke test for the Felucca unity-build overlay.
 * Include paths point at generated tables and a Felucca tree after
 * apply_felucca_v3_overlay.py has copied the overlay files into firmware/src.
 */
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

void fm6_bench_overlay_smoke_entry(void)
{
    fm6_bench_ac_felucca_run();
}
