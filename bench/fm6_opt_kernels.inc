/* V2 operator-kernel experiments.
 *
 * This file is included after fm6_workload.inc, so it intentionally reuses
 * Felucca's exact FM6_N, fm6_sin() and fm6_op_run().
 *
 * The no-interpolation experiment uses 1024 steps per quadrant (4096 discrete
 * full-cycle positions) with a 1025-entry quarter-wave table. That mirrors the
 * phase resolution observed in the stock FM-1 operator path more closely than
 * simply removing interpolation from Felucca's 1024-point full-wave table.
 */

#ifndef FM6_OPT_KERNELS_INC
#define FM6_OPT_KERNELS_INC

#include "bench_qsine4096.h"

#define BENCH_NOINLINE __attribute__((noinline))

static inline int32_t bench_sin_nointerp(int32_t ph)
{
    uint32_t p = ((uint32_t)ph >> 12) & 4095u;
    uint32_t q = p >> 10;
    uint32_t x = p & 1023u;
    if (q & 1u)
        x = 1024u - x;
    int32_t y = BENCH_QSINE4096[x];
    return (q & 2u) ? -y : y;
}

#define BENCH_OP_LOOP(SINFN, PH, STORE)                                          \
    for (i = 0; i < FM6_N; ++i) {                                               \
        g += dg;                                                                 \
        out[i] STORE (int32_t)(((int64_t)SINFN(PH) * g) >> 15);                 \
        phase = (int32_t)((uint32_t)phase + (uint32_t)freq);                    \
    }

BENCH_NOINLINE void bench_op_run_nointerp(int32_t *out, const int32_t *in,
                                           int32_t phase, int32_t freq,
                                           int32_t g1, int32_t g2, int add)
{
    int32_t dg = (g2 - g1 + (FM6_N >> 1)) >> FM6_LG_N, g = g1;
    uint32_t i;
    if (in && add)
        BENCH_OP_LOOP(bench_sin_nointerp, phase + in[i], +=)
    else if (in)
        BENCH_OP_LOOP(bench_sin_nointerp, phase + in[i], =)
    else if (add)
        BENCH_OP_LOOP(bench_sin_nointerp, phase, +=)
    else
        BENCH_OP_LOOP(bench_sin_nointerp, phase, =)
}

#undef BENCH_OP_LOOP

typedef struct {
    int32_t phase[3];
    int32_t freq[3];
    int32_t g1[3];
    int32_t g2[3];
} bench_chain3_params_t;

BENCH_NOINLINE void bench_chain3_generic_interp(int32_t *out, const bench_chain3_params_t *p)
{
    int32_t b1[FM6_N];
    int32_t b2[FM6_N];
    fm6_op_run(b1, 0, p->phase[0], p->freq[0], p->g1[0], p->g2[0], 0);
    fm6_op_run(b2, b1, p->phase[1], p->freq[1], p->g1[1], p->g2[1], 0);
    fm6_op_run(out, b2, p->phase[2], p->freq[2], p->g1[2], p->g2[2], 0);
}

BENCH_NOINLINE void bench_chain3_generic_nointerp(int32_t *out, const bench_chain3_params_t *p)
{
    int32_t b1[FM6_N];
    int32_t b2[FM6_N];
    bench_op_run_nointerp(b1, 0, p->phase[0], p->freq[0], p->g1[0], p->g2[0], 0);
    bench_op_run_nointerp(b2, b1, p->phase[1], p->freq[1], p->g1[1], p->g2[1], 0);
    bench_op_run_nointerp(out, b2, p->phase[2], p->freq[2], p->g1[2], p->g2[2], 0);
}

BENCH_NOINLINE void bench_chain3_fused_interp(int32_t *out, const bench_chain3_params_t *p)
{
    int32_t p0 = p->phase[0], p1 = p->phase[1], p2 = p->phase[2];
    int32_t f0 = p->freq[0], f1 = p->freq[1], f2 = p->freq[2];
    int32_t d0 = (p->g2[0] - p->g1[0] + (FM6_N >> 1)) >> FM6_LG_N;
    int32_t d1 = (p->g2[1] - p->g1[1] + (FM6_N >> 1)) >> FM6_LG_N;
    int32_t d2 = (p->g2[2] - p->g1[2] + (FM6_N >> 1)) >> FM6_LG_N;
    int32_t a0 = p->g1[0], a1 = p->g1[1], a2 = p->g1[2];

    for (uint32_t i = 0; i < FM6_N; ++i) {
        a0 += d0;
        int32_t y0 = (int32_t)(((int64_t)fm6_sin(p0) * a0) >> 15);
        p0 = (int32_t)((uint32_t)p0 + (uint32_t)f0);

        a1 += d1;
        int32_t y1 = (int32_t)(((int64_t)fm6_sin(p1 + y0) * a1) >> 15);
        p1 = (int32_t)((uint32_t)p1 + (uint32_t)f1);

        a2 += d2;
        out[i] = (int32_t)(((int64_t)fm6_sin(p2 + y1) * a2) >> 15);
        p2 = (int32_t)((uint32_t)p2 + (uint32_t)f2);
    }
}

BENCH_NOINLINE void bench_chain3_fused_nointerp(int32_t *out, const bench_chain3_params_t *p)
{
    int32_t p0 = p->phase[0], p1 = p->phase[1], p2 = p->phase[2];
    int32_t f0 = p->freq[0], f1 = p->freq[1], f2 = p->freq[2];
    int32_t d0 = (p->g2[0] - p->g1[0] + (FM6_N >> 1)) >> FM6_LG_N;
    int32_t d1 = (p->g2[1] - p->g1[1] + (FM6_N >> 1)) >> FM6_LG_N;
    int32_t d2 = (p->g2[2] - p->g1[2] + (FM6_N >> 1)) >> FM6_LG_N;
    int32_t a0 = p->g1[0], a1 = p->g1[1], a2 = p->g1[2];

    for (uint32_t i = 0; i < FM6_N; ++i) {
        a0 += d0;
        int32_t y0 = (int32_t)(((int64_t)bench_sin_nointerp(p0) * a0) >> 15);
        p0 = (int32_t)((uint32_t)p0 + (uint32_t)f0);

        a1 += d1;
        int32_t y1 = (int32_t)(((int64_t)bench_sin_nointerp(p1 + y0) * a1) >> 15);
        p1 = (int32_t)((uint32_t)p1 + (uint32_t)f1);

        a2 += d2;
        out[i] = (int32_t)(((int64_t)bench_sin_nointerp(p2 + y1) * a2) >> 15);
        p2 = (int32_t)((uint32_t)p2 + (uint32_t)f2);
    }
}

#endif
