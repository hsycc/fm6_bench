/* Felucca V4A full-audio benchmark core.
 * Included in the unity TU after the V3 FM6 benchmark core and before audio.c.
 * The real ALNK0 ISR calls the hooks below only while a V4A case is active.
 */
#define V4A_FM6_WORKLOAD_ALREADY_INCLUDED 1
#include "fm6_bench/v4a_audio_workload.inc"

#define V4A_AC_CLOCK_HZ 24000000u
#define V4A_AC_DAC_TICKS_PER_FRAME 544u
#define V4A_AC_DEADLINE_TICKS (V4A_QUANTUM_FRAMES * V4A_AC_DAC_TICKS_PER_FRAME)
#define V4A_AC_N V4A_MEASURE_QUANTA

struct v4a_ac_result {
    uint32_t voices;
    uint32_t deadline_ticks;
    uint32_t dsp_avg, dsp_p95, dsp_p99, dsp_max, dsp_miss;
    uint32_t write_avg, write_p99, write_max;
    uint32_t total_avg, total_p99, total_max, total_miss;
    uint32_t late, sink;
};

static volatile uint8_t v4a_ac_active;
static volatile uint8_t v4a_ac_done;
static volatile uint8_t v4a_ac_voices;
static volatile uint32_t v4a_ac_warm_left;
static volatile uint32_t v4a_ac_index;
static volatile uint32_t v4a_ac_dsp_accum;
static volatile uint32_t v4a_ac_copy_accum;
static volatile uint32_t v4a_ac_late;
static volatile uint32_t v4a_ac_sink;
static uint32_t v4a_ac_dsp[V4A_AC_N];
static uint32_t v4a_ac_copy[V4A_AC_N];
static uint32_t v4a_ac_total[V4A_AC_N];
static struct v4a_ac_result v4a_ac_results[V4A_CASES];

static void v4a_ac_sort(uint32_t *a, uint32_t n)
{
    uint32_t gap, i;
    for (gap = n >> 1; gap; gap >>= 1)
        for (i = gap; i < n; ++i) {
            uint32_t v = a[i], j = i;
            while (j >= gap && a[j - gap] > v) {
                a[j] = a[j - gap];
                j -= gap;
            }
            a[j] = v;
        }
}

static uint32_t v4a_ac_pct(const uint32_t *a, uint32_t pct)
{
    uint32_t rank = (V4A_AC_N * pct + 99u) / 100u;
    if (!rank) rank = 1u;
    if (rank > V4A_AC_N) rank = V4A_AC_N;
    return a[rank - 1u];
}

static uint32_t v4a_ac_avg(const uint32_t *a)
{
    uint64_t s = 0;
    uint32_t i;
    for (i = 0; i < V4A_AC_N; ++i) s += a[i];
    return (uint32_t)(s / V4A_AC_N);
}

static void v4a_ac_start_case(uint32_t voices)
{
    fm1_irq_off();
    bench_init_voices(voices);
    v4a_ac_voices = (uint8_t)voices;
    v4a_ac_warm_left = V4A_WARM_QUANTA;
    v4a_ac_index = 0;
    v4a_ac_dsp_accum = 0;
    v4a_ac_copy_accum = 0;
    v4a_ac_late = 0;
    v4a_ac_sink = 0x7f4a7c15u ^ voices;
    v4a_ac_done = 0;
    v4a_ac_active = 1;
    fm1_irq_on();
}

static void v4a_ac_half_begin(void)
{
    v4a_ac_dsp_accum = 0;
    v4a_ac_copy_accum = 0;
}

/* Called once per CTL=32 block from the real ALNK0 ISR. `dsp` intentionally
 * includes the final copy into the actual DMA half, matching S3's render+
 * mix/pack+transport-buffer fill boundary. `write` keeps that copy as a
 * diagnostic subset. */
static void v4a_ac_audio_block(int32_t *out)
{
    uint32_t a = fm1_ticks();
    uint32_t sounded = v4a_render_stereo_block(v4a_ac_voices);
    uint32_t b = fm1_ticks();
    uint32_t i;
    for (i = 0; i < CTL * 2u; ++i)
        out[i] = v4a_stereo_block[i];
    uint32_t c = fm1_ticks();
    v4a_ac_dsp_accum += c - a;
    v4a_ac_copy_accum += c - b;
    v4a_ac_sink = (v4a_ac_sink << 5) ^ sounded ^ (uint32_t)out[0];
}

/* all_ticks is from ALNK0 entry through rendering/ack, including TIMER5 work
 * that nested inside the audio ISR. late is Felucca's actual DMA-half change
 * test: the hardware moved on before this half finished rendering. */
static void v4a_ac_half_end(uint32_t all_ticks, uint32_t late)
{
    uint32_t i;
    if (v4a_ac_warm_left) {
        --v4a_ac_warm_left;
        return;
    }
    i = v4a_ac_index;
    if (i >= V4A_AC_N)
        return;
    v4a_ac_dsp[i] = v4a_ac_dsp_accum;
    v4a_ac_copy[i] = v4a_ac_copy_accum;
    v4a_ac_total[i] = all_ticks;
    v4a_ac_late += late != 0u;
    ++i;
    v4a_ac_index = i;
    if (i == V4A_AC_N) {
        v4a_ac_active = 0;
        v4a_ac_done = 1;
    }
}

static void v4a_ac_finish_case(uint32_t slot)
{
    struct v4a_ac_result *r = &v4a_ac_results[slot];
    uint32_t i, dm = 0, tm = 0;
    v4a_ac_sort(v4a_ac_dsp, V4A_AC_N);
    v4a_ac_sort(v4a_ac_copy, V4A_AC_N);
    v4a_ac_sort(v4a_ac_total, V4A_AC_N);
    for (i = 0; i < V4A_AC_N; ++i) {
        dm += v4a_ac_dsp[i] > V4A_AC_DEADLINE_TICKS;
        tm += v4a_ac_total[i] > V4A_AC_DEADLINE_TICKS;
    }
    r->voices = v4a_voice_cases[slot];
    r->deadline_ticks = V4A_AC_DEADLINE_TICKS;
    r->dsp_avg = v4a_ac_avg(v4a_ac_dsp);
    r->dsp_p95 = v4a_ac_pct(v4a_ac_dsp, 95u);
    r->dsp_p99 = v4a_ac_pct(v4a_ac_dsp, 99u);
    r->dsp_max = v4a_ac_dsp[V4A_AC_N - 1u];
    r->dsp_miss = dm;
    r->write_avg = v4a_ac_avg(v4a_ac_copy);
    r->write_p99 = v4a_ac_pct(v4a_ac_copy, 99u);
    r->write_max = v4a_ac_copy[V4A_AC_N - 1u];
    r->total_avg = v4a_ac_avg(v4a_ac_total);
    r->total_p99 = v4a_ac_pct(v4a_ac_total, 99u);
    r->total_max = v4a_ac_total[V4A_AC_N - 1u];
    r->total_miss = tm;
    r->late = v4a_ac_late;
    r->sink = v4a_ac_sink;
}
