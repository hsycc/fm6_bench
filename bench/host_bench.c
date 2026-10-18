#define _GNU_SOURCE
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fm6_workload.inc"

#define WARM_BLOCKS 256u
#define MEASURE_BLOCKS 4096u

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

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

static uint32_t pindex(uint32_t n, uint32_t permille)
{
    return (uint32_t)(((uint64_t)(n - 1u) * permille + 999u) / 1000u);
}

static void run_case(uint32_t voices)
{
    static uint64_t t[MEASURE_BLOCKS];
    uint64_t total = 0;
    uint32_t crc = 0, sounded = 0;

    bench_init_voices(voices);
    for (uint32_t i = 0; i < WARM_BLOCKS; ++i)
        sounded ^= bench_render_voices(voices);

    for (uint32_t i = 0; i < MEASURE_BLOCKS; ++i) {
        uint64_t a = ns_now();
        sounded ^= bench_render_voices(voices);
        uint64_t b = ns_now();
        t[i] = b - a;
        total += t[i];
        crc = crc32_update(crc, bench_out, voices * CTL * sizeof(int32_t));
    }

    qsort(t, MEASURE_BLOCKS, sizeof(t[0]), cmp_u64);
    uint64_t avg = total / MEASURE_BLOCKS;
    uint64_t p50 = t[pindex(MEASURE_BLOCKS, 500)];
    uint64_t p95 = t[pindex(MEASURE_BLOCKS, 950)];
    uint64_t p99 = t[pindex(MEASURE_BLOCKS, 990)];
    uint64_t max = t[MEASURE_BLOCKS - 1u];
    double nvps = (double)avg / (double)(voices * CTL);

    printf("voices=%u avg_ns=%" PRIu64 " p50_ns=%" PRIu64 " p95_ns=%" PRIu64
           " p99_ns=%" PRIu64 " max_ns=%" PRIu64 " ns_voice_sample=%.3f crc32=%08x sink=%u\n",
           voices, avg, p50, p95, p99, max, nvps, crc, sounded);
}

int main(void)
{
    static const uint8_t counts[] = {12, 16, 20, 24};
    printf("FM6-BENCH host fs=%u block=%u blocks=%u warm=%u\n", FS, CTL,
           MEASURE_BLOCKS, WARM_BLOCKS);
    for (uint32_t i = 0; i < sizeof(counts); ++i)
        run_case(counts[i]);
    return 0;
}
