#pragma once
#include <stdint.h>

#define BENCH_RT_CASES 6u

typedef struct {
    uint32_t voices;
    uint32_t clock_hz;
    uint32_t cpu_hz;
    uint32_t sample_rate;
    uint32_t block_samples;
    uint32_t read_overhead;
    uint32_t deadline_ticks;
    uint32_t min_ticks;
    uint32_t p50_ticks;
    uint32_t p95_ticks;
    uint32_t p99_ticks;
    uint32_t max_ticks;
    uint32_t avg_ticks;
    uint32_t misses;
    uint32_t util_permille;
    uint32_t avg_equiv_cycles;
    uint32_t deadline_equiv_cycles;
    uint32_t sink;
} bench_rt_result_t;
