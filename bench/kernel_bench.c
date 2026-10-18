#define _GNU_SOURCE
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "fm6_workload.inc"
#include "fm6_opt_kernels.inc"

#define KERNEL_VOICES 24u
#define KERNEL_WARM_BLOCKS 512u
#define KERNEL_MEASURE_BLOCKS 4096u

typedef void (*chain3_fn_t)(int32_t *, const bench_chain3_params_t *);

static bench_chain3_params_t params[KERNEL_VOICES];
static int32_t outbuf[KERNEL_VOICES][FM6_N];
static int32_t refbuf[KERNEL_VOICES][FM6_N];
static volatile uint32_t kernel_sink;

static uint64_t ns_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static uint32_t crc32_update(uint32_t crc, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    crc = ~crc;
    while (len--) {
        uint32_t x = (crc ^ *p++) & 0xffu;
        for (unsigned k = 0; k < 8; ++k)
            x = (x >> 1) ^ (0xedb88320u & (0u - (x & 1u)));
        crc = (crc >> 8) ^ x;
    }
    return ~crc;
}

static void init_params(void)
{
    for (uint32_t v = 0; v < KERNEL_VOICES; ++v) {
        params[v].phase[0] = (int32_t)(0x00123456u + v * 0x00051371u);
        params[v].phase[1] = (int32_t)(0x00432110u + v * 0x00031f29u);
        params[v].phase[2] = (int32_t)(0x00765432u + v * 0x00071d13u);

        /* Q24 cycles/sample: representative audio-rate ratios at 44.1 kHz. */
        params[v].freq[0] = 61000 + (int32_t)(v * 1733u);
        params[v].freq[1] = 97000 + (int32_t)(v * 2551u);
        params[v].freq[2] = 151000 + (int32_t)(v * 3253u);

        /* Gain ramps force the same per-sample gain interpolation as fm6_op_run. */
        params[v].g1[0] = (1 << 20) + (int32_t)(v * 4096u);
        params[v].g2[0] = (1 << 22) + (int32_t)(v * 8192u);
        params[v].g1[1] = (1 << 21) + (int32_t)(v * 3072u);
        params[v].g2[1] = (1 << 23) + (int32_t)(v * 6144u);
        params[v].g1[2] = (1 << 22) + (int32_t)(v * 2048u);
        params[v].g2[2] = (1 << 24) - 65536 + (int32_t)(v * 1024u);
    }
}

static void advance_phases(void)
{
    for (uint32_t v = 0; v < KERNEL_VOICES; ++v)
        for (uint32_t k = 0; k < 3u; ++k)
            params[v].phase[k] = (int32_t)((uint32_t)params[v].phase[k] +
                                           ((uint32_t)params[v].freq[k] << FM6_LG_N));
}

static uint32_t render_block(chain3_fn_t fn, int32_t dst[KERNEL_VOICES][FM6_N])
{
    uint32_t s = 0;
    for (uint32_t v = 0; v < KERNEL_VOICES; ++v) {
        fn(dst[v], &params[v]);
        s ^= (uint32_t)dst[v][v & (FM6_N - 1u)];
    }
    advance_phases();
    return s;
}

static int verify_exact(chain3_fn_t generic_fn, chain3_fn_t fused_fn, const char *name)
{
    init_params();
    for (uint32_t v = 0; v < KERNEL_VOICES; ++v) {
        generic_fn(refbuf[v], &params[v]);
        fused_fn(outbuf[v], &params[v]);
    }
    int ok = memcmp(refbuf, outbuf, sizeof refbuf) == 0;
    printf("equiv mode=%s result=%s\n", name, ok ? "PASS" : "FAIL");
    return ok;
}

static void fidelity_nointerp(void)
{
    long double signal_sq = 0.0L, error_sq = 0.0L;
    uint64_t abs_sum = 0;
    uint32_t max_abs = 0;

    init_params();
    for (uint32_t b = 0; b < 257u; ++b) {
        for (uint32_t v = 0; v < KERNEL_VOICES; ++v) {
            bench_chain3_fused_interp(refbuf[v], &params[v]);
            bench_chain3_fused_nointerp(outbuf[v], &params[v]);
        }
        advance_phases();
    }

    for (uint32_t v = 0; v < KERNEL_VOICES; ++v) {
        for (uint32_t i = 0; i < FM6_N; ++i) {
            int64_t r = refbuf[v][i];
            int64_t e = (int64_t)outbuf[v][i] - r;
            uint64_t a = (uint64_t)(e < 0 ? -e : e);
            signal_sq += (long double)r * (long double)r;
            error_sq += (long double)e * (long double)e;
            abs_sum += a;
            if (a > max_abs)
                max_abs = (uint32_t)a;
        }
    }

    long double n = (long double)(KERNEL_VOICES * FM6_N);
    double snr_db = error_sq > 0.0L ? 10.0 * log10((double)(signal_sq / error_sq)) : 999.0;
    double rms_signal = sqrt((double)(signal_sq / n));
    double rms_error = sqrt((double)(error_sq / n));
    double mean_abs = (double)abs_sum / (double)(KERNEL_VOICES * FM6_N);
    printf("fidelity mode=nointerp snr_db=%.3f rms_signal=%.3f rms_error=%.3f mean_abs=%.3f max_abs=%u\n",
           snr_db, rms_signal, rms_error, mean_abs, max_abs);
}

static uint64_t run_timed(chain3_fn_t fn, uint32_t *crc_out)
{
    uint64_t total = 0;
    uint32_t crc = 0, s = 0;

    init_params();
    for (uint32_t b = 0; b < KERNEL_WARM_BLOCKS; ++b)
        s ^= render_block(fn, outbuf);

    for (uint32_t b = 0; b < KERNEL_MEASURE_BLOCKS; ++b) {
        uint64_t a = ns_now();
        s ^= render_block(fn, outbuf);
        uint64_t z = ns_now();
        total += z - a;
        crc = crc32_update(crc, outbuf, sizeof outbuf);
    }

    kernel_sink ^= s;
    *crc_out = crc;
    return total / KERNEL_MEASURE_BLOCKS;
}

static void report_timing(const char *name, chain3_fn_t fn, uint64_t generic_ns)
{
    uint32_t crc;
    uint64_t avg = run_timed(fn, &crc);
    double ns_voice_sample = (double)avg / (double)(KERNEL_VOICES * FM6_N);
    double speedup = generic_ns ? (double)generic_ns / (double)avg : 1.0;
    printf("kernel mode=%s avg_ns=%" PRIu64 " ns_voice_sample=%.3f speedup_vs_generic_interp=%.4f crc32=%08x\n",
           name, avg, ns_voice_sample, speedup, crc);
}

int main(void)
{
    int ok = 1;
    printf("FM6-KERNEL-BENCH fs=%u block=%u voices=%u blocks=%u warm=%u\n",
           FS, FM6_N, KERNEL_VOICES, KERNEL_MEASURE_BLOCKS, KERNEL_WARM_BLOCKS);

    ok &= verify_exact(bench_chain3_generic_interp, bench_chain3_fused_interp, "chain3_interp");
    ok &= verify_exact(bench_chain3_generic_nointerp, bench_chain3_fused_nointerp, "chain3_nointerp");
    fidelity_nointerp();

    uint32_t crc;
    uint64_t generic_ns = run_timed(bench_chain3_generic_interp, &crc);
    printf("kernel mode=generic_interp avg_ns=%" PRIu64 " ns_voice_sample=%.3f speedup_vs_generic_interp=1.0000 crc32=%08x\n",
           generic_ns, (double)generic_ns / (double)(KERNEL_VOICES * FM6_N), crc);
    report_timing("fused_interp", bench_chain3_fused_interp, generic_ns);
    report_timing("generic_nointerp", bench_chain3_generic_nointerp, generic_ns);
    report_timing("fused_nointerp", bench_chain3_fused_nointerp, generic_ns);

    printf("kernel_sink=%u\n", kernel_sink);
    return ok ? 0 : 2;
}
