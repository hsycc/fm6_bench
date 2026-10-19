#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

static uint32_t host_clock_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec);
}

#define BENCH_RT_PLATFORM "host-ns"
#define BENCH_RT_CLOCK_NOW() host_clock_now()
#define BENCH_RT_CLOCK_HZ 1000000000u
#define BENCH_RT_CPU_HZ 0u
#include "target_runtime_core.inc"

int main(void)
{
    bench_rt_run_all();
    printf("FM6-V3 platform=%s clock_hz=%u cpu_hz=%u sr=%u block=%u samples=%u\n",
           BENCH_RT_PLATFORM, BENCH_RT_CLOCK_HZ, BENCH_RT_CPU_HZ,
           BENCH_RT_SAMPLE_RATE, (unsigned)CTL, (unsigned)BENCH_RT_SAMPLES);
    for (uint32_t i = 0; i < BENCH_RT_CASES; ++i) {
        const bench_rt_result_t *r = &bench_rt_results[i];
        printf("FM6-V3 voices=%u overhead=%u deadline=%u min=%u avg=%u p50=%u p95=%u p99=%u max=%u "
               "miss=%u util_permille=%u equiv_avg_cycles=%u equiv_deadline_cycles=%u sink=%08x\n",
               r->voices, r->read_overhead, r->deadline_ticks, r->min_ticks, r->avg_ticks,
               r->p50_ticks, r->p95_ticks, r->p99_ticks, r->max_ticks, r->misses,
               r->util_permille, r->avg_equiv_cycles, r->deadline_equiv_cycles, r->sink);
    }
    puts("FM6-V3 DONE");
    return 0;
}
