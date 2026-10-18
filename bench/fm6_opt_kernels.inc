/* V2 operator-kernel experiments.
 *
 * This file is included after fm6_workload.inc, so it intentionally reuses
 * Felucca's exact FM6_N, SINE table, fm6_sin() and fm6_op_run().
 *
 * The two experiments mirror optimizations observed in reverse engineering of
 * the stock FM-1 firmware:
 *   1) operator sine lookup without interpolation;
 *   2) a fused three-operator cascade that avoids two intermediate block
 *      buffers.
 *
 * The fused interpolated chain is required to be bit-exact with three generic
 * fm6_op_run() calls. The no-interpolation variants are intentionally not
 * bit-exact to the interpolated reference; host_bench reports that output
 * difference separately.
 */

#ifndef FM6_OPT_KERNELS_INC
#define FM6_OPT_KERNELS_INC

#define BENCH_NOINLINE __attribute__((noinline))

static inline int32_t bench_sin_nointerp(int32_t ph)
{
    return SINE[((uint32_t)ph >> 14) & 1023u];
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

/* Generic three-op cascade using the exact Felucca operator kernel. */
BENCH_NOINLINE void bench_chain3_generic_interp(int32_t *out, const bench_chain3_params_t *p)
{
    int32_t b1[FM6_N];
    int32_t b2[FM6_N];
    fm6_op_run(b1, 0, p->phase[0], p->freq[0], p->g1[0], p->g2[0], 0);
    fm6_op_run(b2, b1, p->phase[1], p->freq[1], p->g1[1], p->g2[1], 0);
    fm6_op_run(out, b2, p->phase[2], p->freq[2], p->g1[2], p->g2[2], 0);
}

/* Same generic topology, but with the stock-FM-1-style no-interp operator. */
BENCH_NOINLINE void bench_chain3_generic_nointerp(int32_t *out, const bench_chain3_params_t *p)
{
    int32_t b1[FM6_N];
    int32_t b2[FM6_N];
    bench_op_run_nointerp(b1, 0, p->phase[0], p->freq[0], p->g1[0], p->g2[0], 0);
    bench_op_run_nointerp(b2, b1, p->phase[1], p->freq[1], p->g1[1], p->g2[1], 0);
    bench_op_run_nointerp(out, b2, p->phase[2], p->freq[2], p->g1[2], p->g2[2], 0);
}

/* Fused three-op cascade: identical math/order to generic_interp, but each
 * sample flows op0 -> op1 -> op2 in registers instead of through b1/b2. */
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
