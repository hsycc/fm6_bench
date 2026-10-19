/* V3 target runtime benchmark core.
 *
 * The including translation unit supplies:
 *   BENCH_RT_PLATFORM       string literal
 *   BENCH_RT_CLOCK_NOW()    monotonically wrapping uint32_t counter
 *   BENCH_RT_CLOCK_HZ       counter frequency in Hz
 * Optional:
 *   BENCH_RT_CPU_HZ         CPU frequency for equivalent-cycle reporting; 0 if unknown
 *   BENCH_RT_SAMPLE_RATE    synthesis sample rate (default 44100)
 *   BENCH_RT_ENTER/EXIT     isolate one measured block (default no-op)
 *   BENCH_RT_AFTER_BLOCK    service hook outside timing (default no-op)
 *
 * A measurement is one FM6 control block (CTL = 32 samples) for N complete
 * six-operator voices. Clock read overhead is calibrated and subtracted.
 * Percentiles are over BENCH_RT_SAMPLES independent block timings.
 * Only bench_render_voices() is inside the measured interval; result/sink
 * bookkeeping and BENCH_RT_AFTER_BLOCK happen after the end clock read.
 */
#ifndef BENCH_TARGET_RUNTIME_CORE_INC
#define BENCH_TARGET_RUNTIME_CORE_INC

#include <stdint.h>
#include "target_runtime_result.h"

#ifndef BENCH_RT_PLATFORM
#error "BENCH_RT_PLATFORM must be defined"
#endif
#ifndef BENCH_RT_CLOCK_NOW
#error "BENCH_RT_CLOCK_NOW() must be defined"
#endif
#ifndef BENCH_RT_CLOCK_HZ
#error "BENCH_RT_CLOCK_HZ must be defined"
#endif
#ifndef BENCH_RT_CPU_HZ
#define BENCH_RT_CPU_HZ 0u
#endif
#ifndef BENCH_RT_SAMPLE_RATE
#define BENCH_RT_SAMPLE_RATE 44100u
#endif
#ifndef BENCH_RT_ENTER
#define BENCH_RT_ENTER() do { } while (0)
#endif
#ifndef BENCH_RT_EXIT
#define BENCH_RT_EXIT() do { } while (0)
#endif
#ifndef BENCH_RT_AFTER_BLOCK
#define BENCH_RT_AFTER_BLOCK() do { } while (0)
#endif
#ifndef BENCH_RT_SAMPLES
#define BENCH_RT_SAMPLES 1024u
#endif
#ifndef BENCH_RT_WARM_BLOCKS
#define BENCH_RT_WARM_BLOCKS 128u
#endif

#if BENCH_RT_SAMPLES < 100u
#error "BENCH_RT_SAMPLES must be at least 100 for p99"
#endif

#include "fm6_workload.inc"

static const uint8_t bench_rt_voice_cases[BENCH_RT_CASES] = {1u, 8u, 12u, 16u, 20u, 24u};
bench_rt_result_t bench_rt_results[BENCH_RT_CASES];
volatile uint32_t bench_rt_sink;
static uint32_t bench_rt_samples[BENCH_RT_SAMPLES];

static uint32_t bench_rt_mix_sink(uint32_t sink, uint32_t value, uint32_t seq)
{
    /* Outside the timed interval. Rotation + sequence makes cancellation over
     * an even number of identical return values unlikely while keeping the
     * render call observably consumed. */
    sink = (sink << 5) | (sink >> 27);
    return sink ^ value ^ (0x9e3779b9u + seq * 0x85ebca6bu);
}

static uint32_t bench_rt_read_overhead(void)
{
    uint32_t best = 0xffffffffu;
    for (uint32_t i = 0; i < 128u; ++i) {
        uint32_t a, b;
        BENCH_RT_ENTER();
        a = (uint32_t)BENCH_RT_CLOCK_NOW();
        b = (uint32_t)BENCH_RT_CLOCK_NOW();
        BENCH_RT_EXIT();
        uint32_t d = b - a;
        if (d < best)
            best = d;
    }
    return best == 0xffffffffu ? 0u : best;
}

/* Shell sort: sorting happens after timing, so it cannot perturb samples. */
static void bench_rt_sort(uint32_t *a, uint32_t n)
{
    for (uint32_t gap = n >> 1; gap; gap >>= 1) {
        for (uint32_t i = gap; i < n; ++i) {
            uint32_t v = a[i], j = i;
            while (j >= gap && a[j - gap] > v) {
                a[j] = a[j - gap];
                j -= gap;
            }
            a[j] = v;
        }
    }
}

static uint32_t bench_rt_pct_index(uint32_t pct)
{
    /* nearest-rank percentile, converted to a zero-based index */
    uint32_t rank = (BENCH_RT_SAMPLES * pct + 99u) / 100u;
    if (!rank)
        rank = 1u;
    if (rank > BENCH_RT_SAMPLES)
        rank = BENCH_RT_SAMPLES;
    return rank - 1u;
}

static void bench_rt_measure(uint32_t voices, bench_rt_result_t *r)
{
    uint64_t sum = 0;
    uint32_t misses = 0, overhead = bench_rt_read_overhead();
    const uint32_t deadline = (uint32_t)(((uint64_t)BENCH_RT_CLOCK_HZ * (uint64_t)CTL +
                                          (uint64_t)BENCH_RT_SAMPLE_RATE - 1u) /
                                         (uint64_t)BENCH_RT_SAMPLE_RATE);

    bench_init_voices(voices);
    for (uint32_t i = 0; i < BENCH_RT_WARM_BLOCKS; ++i) {
        uint32_t sounded = bench_render_voices(voices);
        bench_rt_sink = bench_rt_mix_sink(bench_rt_sink, sounded, i);
        BENCH_RT_AFTER_BLOCK();
    }

    for (uint32_t i = 0; i < BENCH_RT_SAMPLES; ++i) {
        uint32_t a, b, d, sounded;
        BENCH_RT_ENTER();
        a = (uint32_t)BENCH_RT_CLOCK_NOW();
        sounded = bench_render_voices(voices);
        b = (uint32_t)BENCH_RT_CLOCK_NOW();
        BENCH_RT_EXIT();
        bench_rt_sink = bench_rt_mix_sink(bench_rt_sink, sounded,
                                           BENCH_RT_WARM_BLOCKS + i);
        BENCH_RT_AFTER_BLOCK();
        d = b - a;
        d = d > overhead ? d - overhead : 0u;
        bench_rt_samples[i] = d;
        sum += d;
        if (d > deadline)
            ++misses;
    }

    bench_rt_sort(bench_rt_samples, BENCH_RT_SAMPLES);
    r->voices = voices;
    r->clock_hz = BENCH_RT_CLOCK_HZ;
    r->cpu_hz = BENCH_RT_CPU_HZ;
    r->sample_rate = BENCH_RT_SAMPLE_RATE;
    r->block_samples = CTL;
    r->read_overhead = overhead;
    r->deadline_ticks = deadline;
    r->min_ticks = bench_rt_samples[0];
    r->p50_ticks = bench_rt_samples[bench_rt_pct_index(50u)];
    r->p95_ticks = bench_rt_samples[bench_rt_pct_index(95u)];
    r->p99_ticks = bench_rt_samples[bench_rt_pct_index(99u)];
    r->max_ticks = bench_rt_samples[BENCH_RT_SAMPLES - 1u];
    r->avg_ticks = (uint32_t)(sum / BENCH_RT_SAMPLES);
    r->misses = misses;
    r->util_permille = deadline ? (uint32_t)(((uint64_t)r->avg_ticks * 1000u) / deadline) : 0u;
    if (BENCH_RT_CPU_HZ) {
        r->avg_equiv_cycles = (uint32_t)(((uint64_t)r->avg_ticks * BENCH_RT_CPU_HZ +
                                           BENCH_RT_CLOCK_HZ / 2u) /
                                          BENCH_RT_CLOCK_HZ);
        r->deadline_equiv_cycles = (uint32_t)(((uint64_t)deadline * BENCH_RT_CPU_HZ +
                                                BENCH_RT_CLOCK_HZ / 2u) /
                                               BENCH_RT_CLOCK_HZ);
    } else {
        r->avg_equiv_cycles = 0u;
        r->deadline_equiv_cycles = 0u;
    }
    r->sink = bench_rt_sink;
}

static void bench_rt_run_all(void)
{
    bench_rt_sink = 0x6d2b79f5u;
    for (uint32_t i = 0; i < BENCH_RT_CASES; ++i)
        bench_rt_measure(bench_rt_voice_cases[i], &bench_rt_results[i]);
}

#endif
