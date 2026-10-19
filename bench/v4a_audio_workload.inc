/* V4A shared audio-block workload.
 *
 * Extends the V3 FM6 stress workload with a deterministic mono mix and stereo
 * 32-bit pack.  The FM synthesis source/patch remains identical to V3; V4A
 * adds only the work a real audio transport needs after the voices render.
 */
#ifndef FM6_BENCH_V4A_AUDIO_WORKLOAD_INC
#define FM6_BENCH_V4A_AUDIO_WORKLOAD_INC

#include <stdint.h>
#include "fm6_workload.inc"

#ifndef V4A_SAMPLE_RATE
#define V4A_SAMPLE_RATE 44100u
#endif
#ifndef V4A_MEASURE_BLOCKS
#define V4A_MEASURE_BLOCKS 1024u
#endif
#ifndef V4A_WARM_BLOCKS
#define V4A_WARM_BLOCKS 128u
#endif

static int32_t v4a_stereo[CTL * 2u];

static int32_t v4a_sat_i32(int64_t x)
{
    if (x > 2147483647LL)
        return 2147483647;
    if (x < (-2147483647LL - 1LL))
        return (-2147483647 - 1);
    return (int32_t)x;
}

/* Render complete FM6 voices, sum them, normalize only to prevent arithmetic
 * clipping in the benchmark transport, and duplicate to L/R.  The division is
 * intentionally part of V4A's post-FM audio cost on every target. */
__attribute__((noinline)) static uint32_t v4a_render_stereo(uint32_t nvoice)
{
    uint32_t sounded = bench_render_voices(nvoice);
    for (uint32_t i = 0; i < CTL; ++i) {
        int64_t s = 0;
        for (uint32_t v = 0; v < nvoice; ++v)
            s += bench_out[v][i];
        if (nvoice)
            s /= (int32_t)nvoice;
        int32_t q = v4a_sat_i32(s);
        v4a_stereo[2u * i] = q;
        v4a_stereo[2u * i + 1u] = q;
    }
    return sounded;
}

static const uint8_t v4a_voice_cases[] = {1u, 8u, 12u, 16u, 20u, 24u};
#define V4A_CASES ((uint32_t)(sizeof v4a_voice_cases / sizeof v4a_voice_cases[0]))

#endif
