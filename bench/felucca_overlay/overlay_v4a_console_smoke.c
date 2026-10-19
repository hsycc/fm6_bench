/* Compile-only smoke for fm6_v4a_console.inc. */
#include <stdint.h>

#define CTL 32u
#define V4A_AC_N 256u
#define V4A_QUANTUM_FRAMES 128u
#define V4A_CASES 6u

struct v4a_ac_result {
    uint32_t voices;
    uint32_t deadline_ticks;
    uint32_t dsp_avg, dsp_p95, dsp_p99, dsp_max, dsp_miss;
    uint32_t write_avg, write_p99, write_max;
    uint32_t total_avg, total_p99, total_max, total_miss;
    uint32_t late, sink;
};
static struct v4a_ac_result v4a_ac_results[V4A_CASES];
static const uint8_t v4a_voice_cases[V4A_CASES] = {1u, 8u, 12u, 16u, 20u, 24u};
static volatile uint8_t v4a_ac_done;

static void con_puts(const char *s) { (void)s; }
static void con_dec(int32_t v) { (void)v; }
static void con_hex(uint32_t v, uint32_t digits) { (void)v; (void)digits; }
static void con_fm6bench_u32(const char *k, uint32_t v) { (void)k; (void)v; }
static void fm1_wdt_feed(void) { }
static void v4a_ac_start_case(uint32_t v) { (void)v; v4a_ac_done = 1u; }
static void v4a_ac_finish_case(uint32_t i) { (void)i; }

#include "fm6_v4a_console.inc"

void fm6_v4a_console_smoke_entry(void)
{
    con_fm6audiobench();
}
