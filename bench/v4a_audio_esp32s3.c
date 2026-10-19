/* ESP32-S3 V4A: FM6 + stereo pack + real I2S TX DMA scheduling.
 *
 * This is deliberately separate from V3's isolated raw-DSP timing. V4A keeps
 * a real 44.1 kHz I2S transport running and records both synthesis/mix cost and
 * the time spent handing each 32-frame block to the TX DMA path.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sdkconfig.h"
#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_err.h"
#include "driver/i2s_std.h"

#define V4A_CPU_HZ ((uint32_t)CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1000000u)
#define V4A_DMA_DESC 4u
#define V4A_WRITE_TIMEOUT_MS 1000u
#define V4A_STAT_N V4A_MEASURE_BLOCKS

#include "v4a_audio_workload.inc"

static volatile uint32_t v4a_dma_sent;
static volatile uint32_t v4a_dma_qovf;
static uint32_t v4a_dsp[V4A_STAT_N];
static uint32_t v4a_write[V4A_STAT_N];
static uint32_t v4a_total[V4A_STAT_N];
static volatile uint32_t v4a_sink;

static bool IRAM_ATTR v4a_on_sent(i2s_chan_handle_t handle, i2s_event_data_t *event, void *user)
{
    (void)handle;
    (void)event;
    (void)user;
    ++v4a_dma_sent;
    return false;
}

static bool IRAM_ATTR v4a_on_send_q_ovf(i2s_chan_handle_t handle, i2s_event_data_t *event, void *user)
{
    (void)handle;
    (void)event;
    (void)user;
    ++v4a_dma_qovf;
    return false;
}

static void v4a_sort(uint32_t *a, uint32_t n)
{
    for (uint32_t gap = n >> 1; gap; gap >>= 1) {
        for (uint32_t i = gap; i < n; ++i) {
            uint32_t v = a[i], j = i;
            while (j >= gap && a[j - gap] > v) {
                a[j] = a[j - gap];
                j -= gap;
            }
            a[j] = v;
        }
    }
}

static uint32_t v4a_pct(const uint32_t *a, uint32_t pct)
{
    uint32_t rank = (V4A_STAT_N * pct + 99u) / 100u;
    if (!rank)
        rank = 1u;
    if (rank > V4A_STAT_N)
        rank = V4A_STAT_N;
    return a[rank - 1u];
}

static uint32_t v4a_avg(const uint32_t *a)
{
    uint64_t s = 0;
    for (uint32_t i = 0; i < V4A_STAT_N; ++i)
        s += a[i];
    return (uint32_t)(s / V4A_STAT_N);
}

static i2s_chan_handle_t v4a_i2s_start(uint32_t *produced)
{
    i2s_chan_handle_t tx = NULL;
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan.dma_desc_num = V4A_DMA_DESC;
    chan.dma_frame_num = CTL;
    chan.auto_clear_after_cb = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan, &tx, NULL));

    i2s_std_config_t cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(V4A_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                         I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = CONFIG_FM6_V4A_I2S_BCLK_GPIO,
            .ws = CONFIG_FM6_V4A_I2S_WS_GPIO,
            .dout = CONFIG_FM6_V4A_I2S_DOUT_GPIO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx, &cfg));

    const i2s_event_callbacks_t cbs = {
        .on_recv = NULL,
        .on_recv_q_ovf = NULL,
        .on_sent = v4a_on_sent,
        .on_send_q_ovf = v4a_on_send_q_ovf,
    };
    ESP_ERROR_CHECK(i2s_channel_register_event_callback(tx, &cbs, NULL));

    memset(v4a_stereo, 0, sizeof v4a_stereo);
    const size_t block_bytes = sizeof v4a_stereo;
    *produced = 0;
    for (;;) {
        size_t loaded = 0;
        ESP_ERROR_CHECK(i2s_channel_preload_data(tx, v4a_stereo, block_bytes, &loaded));
        *produced += (uint32_t)(loaded / block_bytes);
        if (loaded < block_bytes)
            break;
    }
    ESP_ERROR_CHECK(i2s_channel_enable(tx));
    return tx;
}

static void v4a_i2s_stop(i2s_chan_handle_t tx)
{
    ESP_ERROR_CHECK(i2s_channel_disable(tx));
    ESP_ERROR_CHECK(i2s_del_channel(tx));
}

static int v4a_submit(i2s_chan_handle_t tx, uint32_t *produced)
{
    size_t written = 0;
    esp_err_t e = i2s_channel_write(tx, v4a_stereo, sizeof v4a_stereo,
                                    &written, V4A_WRITE_TIMEOUT_MS);
    if (e != ESP_OK || written != sizeof v4a_stereo)
        return 0;
    ++*produced;
    return 1;
}

static void v4a_run_case(uint32_t voices)
{
    const uint32_t deadline = (uint32_t)(((uint64_t)V4A_CPU_HZ * CTL +
                                          V4A_SAMPLE_RATE - 1u) / V4A_SAMPLE_RATE);
    uint32_t produced = 0, write_fail = 0, starve_observed = 0;
    uint32_t dsp_miss = 0, total_miss = 0;
    uint32_t sent0, sent1, qovf0, qovf1;
    i2s_chan_handle_t tx;

    bench_init_voices(voices);
    v4a_dma_sent = 0;
    v4a_dma_qovf = 0;
    tx = v4a_i2s_start(&produced);

    for (uint32_t i = 0; i < V4A_WARM_BLOCKS; ++i) {
        uint32_t sounded = v4a_render_stereo(voices);
        v4a_sink ^= sounded + (uint32_t)v4a_stereo[(i * 2u) % (CTL * 2u)];
        if (!v4a_submit(tx, &produced))
            ++write_fail;
    }

    sent0 = v4a_dma_sent;
    qovf0 = v4a_dma_qovf;
    write_fail = 0;

    for (uint32_t i = 0; i < V4A_STAT_N; ++i) {
        uint32_t a = esp_cpu_get_cycle_count();
        uint32_t sounded = v4a_render_stereo(voices);
        uint32_t b = esp_cpu_get_cycle_count();

        /* If DMA has already consumed every block the producer has ever
         * supplied, the transport ran dry before this replacement block was
         * submitted. This is a direct producer-vs-consumer starvation marker,
         * separate from the driver's send-event queue overflow counter. */
        if ((int32_t)(produced - v4a_dma_sent) <= 0)
            ++starve_observed;

        uint32_t c0 = esp_cpu_get_cycle_count();
        int ok = v4a_submit(tx, &produced);
        uint32_t c1 = esp_cpu_get_cycle_count();
        if (!ok)
            ++write_fail;

        uint32_t dsp = b - a;
        uint32_t wr = c1 - c0;
        uint32_t total = c1 - a;
        v4a_dsp[i] = dsp;
        v4a_write[i] = wr;
        v4a_total[i] = total;
        if (dsp > deadline)
            ++dsp_miss;
        if (total > deadline)
            ++total_miss;
        v4a_sink = (v4a_sink << 5) ^ sounded ^ (uint32_t)v4a_stereo[(i * 2u) % (CTL * 2u)];
    }

    sent1 = v4a_dma_sent;
    qovf1 = v4a_dma_qovf;
    v4a_i2s_stop(tx);

    v4a_sort(v4a_dsp, V4A_STAT_N);
    v4a_sort(v4a_write, V4A_STAT_N);
    v4a_sort(v4a_total, V4A_STAT_N);

    printf("FM6-V4A voices=%u blocks=%u deadline_cycles=%u "
           "dsp_avg=%u dsp_p95=%u dsp_p99=%u dsp_max=%u dsp_miss=%u "
           "write_avg=%u write_p99=%u write_max=%u "
           "total_avg=%u total_p99=%u total_max=%u total_miss=%u "
           "starve=%u write_fail=%u dma_sent=%u send_q_ovf=%u sink=%08x\n",
           (unsigned)voices, (unsigned)V4A_STAT_N, (unsigned)deadline,
           (unsigned)v4a_avg(v4a_dsp), (unsigned)v4a_pct(v4a_dsp, 95u),
           (unsigned)v4a_pct(v4a_dsp, 99u), (unsigned)v4a_dsp[V4A_STAT_N - 1u],
           (unsigned)dsp_miss,
           (unsigned)v4a_avg(v4a_write), (unsigned)v4a_pct(v4a_write, 99u),
           (unsigned)v4a_write[V4A_STAT_N - 1u],
           (unsigned)v4a_avg(v4a_total), (unsigned)v4a_pct(v4a_total, 99u),
           (unsigned)v4a_total[V4A_STAT_N - 1u], (unsigned)total_miss,
           (unsigned)starve_observed, (unsigned)write_fail,
           (unsigned)(sent1 - sent0), (unsigned)(qovf1 - qovf0),
           (unsigned)v4a_sink);
    fflush(stdout);
}

void app_main(void)
{
    printf("FM6-V4A platform=ESP32-S3-I2S cpu_hz=%u sr=%u block=%u dma_desc=%u dma_frame=%u "
           "bclk_gpio=%d ws_gpio=%d dout_gpio=%d\n",
           (unsigned)V4A_CPU_HZ, (unsigned)V4A_SAMPLE_RATE, (unsigned)CTL,
           (unsigned)V4A_DMA_DESC, (unsigned)CTL,
           CONFIG_FM6_V4A_I2S_BCLK_GPIO, CONFIG_FM6_V4A_I2S_WS_GPIO,
           CONFIG_FM6_V4A_I2S_DOUT_GPIO);
    fflush(stdout);

    for (uint32_t i = 0; i < V4A_CASES; ++i)
        v4a_run_case(v4a_voice_cases[i]);

    puts("FM6-V4A DONE");
    fflush(stdout);
    for (;;)
        vTaskDelay(pdMS_TO_TICKS(1000));
}
