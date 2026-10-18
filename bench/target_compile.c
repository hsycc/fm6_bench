#include <stdint.h>
#include "fm6_workload.inc"

volatile uint32_t bench_target_sink;

__attribute__((noinline)) void bench_target_entry(uint32_t voices, uint32_t blocks)
{
    bench_init_voices(voices);
    for (uint32_t i = 0; i < blocks; ++i)
        bench_target_sink ^= bench_render_voices(voices);
}
