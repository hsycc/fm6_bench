/* V4A shared full-audio workload.
 *
 * Felucca/FM-1 services ALNK0 in 128-frame halves and renders that half as
 * four CTL=32 FM blocks. V4A uses the same 128-frame transport quantum on
 * every target so DMA scheduling is compared at the same buffering cadence.
 */
#ifndef FM6_BENCH_V4A_AUDIO_WORKLOAD_INC
#define FM6_BENCH_V4A_AUDIO_WORKLOAD_INC

#include <stdint.h>
#include "fm6_workload.inc"

#ifndef V4A_SAMPLE_RATE
#define V4A_SAMPLE_RATE 44100u
#endif
#ifndef V4A_QUANTUM_FRAMES
#define V4A_QUANTUM_FRAMES 128u
#endif
#ifndef V4A_MEASURE_QUANTA
#define V4A_MEASURE_QUANTA 256u
#endif
#ifndef V4A_WARM_QUANTA
#define V4A_WARM_QUANTA 32u
#endif

#if (V4A_QUANTUM_FRAMES % CTL) != 0
#error "V4A transport quantum must contain a whole number of CTL blocks"
#endif
#define V4A_BLOCKS_PER_QUANTUM (V4A_QUANTUM_FRAMES / CTL)

static int32_t v4a_stereo_block[CTL * 2u];
static int32_t v4a_stereo_quantum[V4A_QUANTUM_FRAMES * 2u];

static int32_t v4a_sat_i32(int64_t x)
{
    if (x > 2147483647LL)
        return 2147483647;
    if (x < (-2147483647LL - 1LL))
        return (-2147483647 - 1);
    return (int32_t)x;
}

/* Render one CTL block, mix complete FM6 voices to mono, normalize only to
 * prevent benchmark arithmetic clipping, and duplicate to L/R. The mix/pack
 * work is intentionally part of the V4A DSP cost on every target. */
__attribute__((noinline)) static uint32_t v4a_render_stereo_block(uint32_t nvoice)
{
    uint32_t sounded = bench_render_voices(nvoice);
    for (uint32_t i = 0; i < CTL; ++i) {
        int64_t s = 0;
        for (uint32_t v = 0; v < nvoice; ++v)
            s += bench_out[v][i];
        if (nvoice)
            s /= (int32_t)nvoice;
        int32_t q = v4a_sat_i32(s);
        v4a_stereo_block[2u * i] = q;
        v4a_stereo_block[2u * i + 1u] = q;
    }
    return sounded;
}

/* One real transport service quantum: four 32-sample FM blocks on FM-1. */
__attribute__((noinline)) static uint32_t v4a_render_quantum(uint32_t nvoice)
{
    uint32_t sounded = 0;
    for (uint32_t b = 0; b < V4A_BLOCKS_PER_QUANTUM; ++b) {
        sounded += v4a_render_stereo_block(nvoice);
        for (uint32_t i = 0; i < CTL * 2u; ++i)
            v4a_stereo_quantum[b * CTL * 2u + i] = v4a_stereo_block[i];
    }
    return sounded;
}

static const uint8_t v4a_voice_cases[] = {1u, 8u, 12u, 16u, 20u, 24u};
#define V4A_CASES ((uint32_t)(sizeof v4a_voice_cases / sizeof v4a_voice_cases[0]))

#endif
