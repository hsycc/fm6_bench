/* Included into Felucca's unity TU immediately after engines.c.
 * Reuses the already-compiled FM6 core and the firmware's 24 MHz TIMER4.
 */
#define BENCH_FM6_CORE_ALREADY_INCLUDED 1
#define BENCH_RT_PLATFORM "AC7911B8-Felucca-TIMER4"
#define BENCH_RT_CLOCK_NOW() fm1_ticks()
#define BENCH_RT_CLOCK_HZ 24000000u
#define BENCH_RT_CPU_HZ 0u
#define BENCH_RT_ENTER() fm1_irq_off()
#define BENCH_RT_EXIT() fm1_irq_on()
#define BENCH_RT_AFTER_BLOCK() fm1_wdt_feed()
#include "fm6_bench/target_runtime_core.inc"

static void fm6_bench_ac_felucca_run(void)
{
    bench_rt_run_all();
}
