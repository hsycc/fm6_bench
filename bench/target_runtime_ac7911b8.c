/* AC7911B8/pi32v2 V3 runtime benchmark entry.
 *
 * Timing source follows Felucca firmware/hal/fm1_time.h: TIMER4 is a
 * free-running 24 MHz counter driven from the crystal. This file does not
 * assume a CPU clock. Define BENCH_AC_CPU_HZ when the running firmware's
 * CPU clock is known and fixed to also populate equivalent-cycle fields.
 *
 * Each measured 32-sample render block disables interrupts only across the
 * two clock reads + render, matching the S3 raw-DSP measurement boundary.
 * Call with normal audio stopped; interrupts run normally between blocks.
 */
#include <stdint.h>

#define BENCH_AC_T4_CON (*(volatile uint32_t *)0x10800u)
#define BENCH_AC_T4_CNT (*(volatile uint32_t *)0x10804u)
#define BENCH_AC_T4_PRD (*(volatile uint32_t *)0x10808u)

#ifndef BENCH_AC_CPU_HZ
#define BENCH_AC_CPU_HZ 0u
#endif

static void bench_ac_time_init(void)
{
    if ((BENCH_AC_T4_CON & 0xFu) == ((2u << 2) | 1u))
        return;
    BENCH_AC_T4_CON = 0u;
    BENCH_AC_T4_PRD = 0xffffffffu;
    BENCH_AC_T4_CNT = 0u;
    BENCH_AC_T4_CON = (2u << 2) | 1u | (1u << 14);
}

static inline void bench_ac_irq_off(void)
{
    __asm__ volatile("cli" ::: "memory");
}

static inline void bench_ac_irq_on(void)
{
    __asm__ volatile("csync\n\tsti" ::: "memory");
}

#define BENCH_RT_PLATFORM "AC7911B8-pi32v2-timer4"
#define BENCH_RT_CLOCK_NOW() (BENCH_AC_T4_CNT)
#define BENCH_RT_CLOCK_HZ 24000000u
#define BENCH_RT_CPU_HZ BENCH_AC_CPU_HZ
#define BENCH_RT_ENTER() bench_ac_irq_off()
#define BENCH_RT_EXIT() bench_ac_irq_on()
#include "target_runtime_core.inc"

void fm6_bench_ac7911b8_run(void)
{
    bench_ac_time_init();
    bench_rt_run_all();
}

const bench_rt_result_t *fm6_bench_ac7911b8_results(uint32_t *count)
{
    if (count)
        *count = BENCH_RT_CASES;
    return bench_rt_results;
}
