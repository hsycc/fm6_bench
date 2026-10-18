#include <stdint.h>
#include "fm6_workload.inc"
#include "fm6_opt_kernels.inc"

volatile uint32_t bench_target_sink;
static int32_t bench_target_out[FM6_N];
static bench_chain3_params_t bench_target_params;

__attribute__((noinline)) void bench_target_entry(uint32_t voices, uint32_t blocks)
{
    bench_init_voices(voices);
    for (uint32_t i = 0; i < blocks; ++i)
        bench_target_sink ^= bench_render_voices(voices);
}

__attribute__((noinline)) void bench_target_opt_entry(uint32_t blocks)
{
    bench_target_params.phase[0] = 0x00123456;
    bench_target_params.phase[1] = 0x00432110;
    bench_target_params.phase[2] = 0x00765432;
    bench_target_params.freq[0] = 61000;
    bench_target_params.freq[1] = 97000;
    bench_target_params.freq[2] = 151000;
    bench_target_params.g1[0] = 1 << 20;
    bench_target_params.g2[0] = 1 << 22;
    bench_target_params.g1[1] = 1 << 21;
    bench_target_params.g2[1] = 1 << 23;
    bench_target_params.g1[2] = 1 << 22;
    bench_target_params.g2[2] = (1 << 24) - 65536;

    for (uint32_t i = 0; i < blocks; ++i) {
        bench_chain3_generic_interp(bench_target_out, &bench_target_params);
        bench_target_sink ^= (uint32_t)bench_target_out[i & (FM6_N - 1u)];
        bench_chain3_fused_interp(bench_target_out, &bench_target_params);
        bench_target_sink ^= (uint32_t)bench_target_out[(i + 3u) & (FM6_N - 1u)];
        bench_chain3_generic_nointerp(bench_target_out, &bench_target_params);
        bench_target_sink ^= (uint32_t)bench_target_out[(i + 7u) & (FM6_N - 1u)];
        bench_chain3_fused_nointerp(bench_target_out, &bench_target_params);
        bench_target_sink ^= (uint32_t)bench_target_out[(i + 11u) & (FM6_N - 1u)];

        for (uint32_t k = 0; k < 3u; ++k)
            bench_target_params.phase[k] = (int32_t)((uint32_t)bench_target_params.phase[k] +
                                                     ((uint32_t)bench_target_params.freq[k] << FM6_LG_N));
    }
}
