#include <stdint.h>
#include <stdio.h>

#include "fm6_workload.inc"

#define DUMP_VOICES 24u
#define DUMP_WARM_BLOCKS 128u
#define DUMP_BLOCKS 512u

int main(void)
{
    uint32_t sink = 0;
    bench_init_voices(DUMP_VOICES);

    for (uint32_t i = 0; i < DUMP_WARM_BLOCKS; ++i)
        sink ^= bench_render_voices(DUMP_VOICES);

    for (uint32_t i = 0; i < DUMP_BLOCKS; ++i) {
        sink ^= bench_render_voices(DUMP_VOICES);
        if (fwrite(bench_out, sizeof(bench_out[0][0]), DUMP_VOICES * CTL, stdout) !=
            DUMP_VOICES * CTL)
            return 2;
    }

    /* Keep the render return value observable without contaminating stdout PCM. */
    return sink == 0xffffffffu ? 3 : 0;
}
