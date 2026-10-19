/* Compile-only smoke test for fm6_bench_console.inc. */
#include <stdint.h>
#include "target_runtime_result.h"

#define CTL 32u
#define BENCH_RT_SAMPLES 1024u
#define FM1_IRQ_ALNK0 11u

bench_rt_result_t bench_rt_results[BENCH_RT_CASES];

static void con_putc(char c) { (void)c; }
static void con_puts(const char *s) { (void)s; }
static void con_dec(int32_t v) { (void)v; }
static void con_hex(uint32_t v, uint32_t digits) { (void)v; (void)digits; }
static void fm1_irq_off(void) { }
static void fm1_irq_on(void) { }
static void fm1_audio_stop(void) { }
static void fm1_irq_mask(uint32_t n) { (void)n; }
static void audio_init(void) { }
static void fm6_bench_ac_felucca_run(void) { }

#include "fm6_bench_console.inc"

void fm6_bench_console_smoke_entry(void)
{
    con_fm6bench();
}
