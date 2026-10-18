/* Shared FM6 workload. Included by host and target translation units. */
#include <stdint.h>
#include <string.h>

#include "felucca_tables.h"

#ifndef FELUCCA_CPU2
#define FELUCCA_CPU2 0
#endif

#include "fm6_core.c"

#define BENCH_MAX_VOICES 24u

static fm6_note_t bench_voices[BENCH_MAX_VOICES];
static int32_t bench_out[BENCH_MAX_VOICES][CTL];
static uint8_t bench_patch[FP_SIZE + 1u];

static void bench_make_heavy_patch(void)
{
    memset(bench_patch, 0, sizeof bench_patch);

    for (uint32_t k = 0; k < 6u; ++k) {
        uint8_t *o = bench_patch + k * FP_OP;
        for (uint32_t i = 0; i < 4u; ++i) {
            o[FP_R1 + i] = 99;
            o[FP_L1 + i] = i == 3u ? 0 : 99;
        }
        o[FP_BP] = 39;
        o[FP_LD] = o[FP_RD] = 0;
        o[FP_LC] = o[FP_RC] = 0;
        o[FP_RS] = 0;
        o[FP_AMS] = 3;
        o[FP_KVS] = 0;
        o[FP_OL] = 99;
        o[FP_MODE] = 0;
        o[FP_FC] = (uint8_t)(k + 1u);
        o[FP_FF] = 0;
        o[FP_DET] = 7;
    }

    for (uint32_t i = 0; i < 4u; ++i) {
        bench_patch[FP_PR1 + i] = 99;
        bench_patch[FP_PL1 + i] = 50;
    }

    /* Mirrors Felucca's own heavy cost case: algorithm 32, six carriers, feedback 7. */
    bench_patch[FP_ALG] = 31;
    bench_patch[FP_FB] = 7;
    bench_patch[FP_OKS] = 1;
    bench_patch[FP_LFS] = 35;
    bench_patch[FP_LFD] = 0;
    bench_patch[FP_LPMD] = 99;
    bench_patch[FP_LAMD] = 99;
    bench_patch[FP_LKS] = 1;
    bench_patch[FP_LFW] = 0;
    bench_patch[FP_LPMS] = 7;
    bench_patch[FP_TRNSP] = 24;
}

static void bench_init_voices(uint32_t nvoice)
{
    memset(bench_voices, 0, sizeof bench_voices);
    memset(bench_out, 0, sizeof bench_out);
    bench_make_heavy_patch();

    for (uint32_t v = 0; v < nvoice; ++v) {
        int note = 48 + (int)(v % 25u);
        fm6_note_init(&bench_voices[v], bench_patch, note, 100, 1);
    }
}

__attribute__((noinline)) static uint32_t bench_render_voices(uint32_t nvoice)
{
    static const int32_t dt[6] = {0, 0, 0, 0, 0, 0};
    uint32_t sounded = 0;

    for (uint32_t v = 0; v < nvoice; ++v) {
        int note = 48 + (int)(v % 25u);
        int32_t logfreq = fm6_note_logfreq(note * 16, 0);
        sounded += (uint32_t)fm6_note_compute(&bench_voices[v], bench_patch, bench_out[v],
                                              1 << 23, 1 << 24, logfreq,
                                              31u, 7, dt, 0);
    }
    return sounded;
}
