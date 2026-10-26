/* V4A transport-baseline screening, not a complete instrument qualification.
 * CCOUNT and the I2S IRQ stay on core 1. The shared FM workload is unchanged.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_err.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if CONFIG_PM_ENABLE
#error "V4A CCOUNT requires fixed-frequency operation; disable PM for this baseline"
#endif
#if CONFIG_FREERTOS_UNICORE
#error "V4A pins its benchmark and I2S allocation to S3 core 1"
#endif
#define V4A_CPU_HZ ((uint32_t)CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1000000u)
#define V4A_DMA_DESC 2u
#define V4A_STAT_N V4A_MEASURE_QUANTA
#include "v4a_audio_workload.inc"

static volatile uint32_t v4a_dma_sent, v4a_dma_qovf;
static uint32_t v4a_dsp[V4A_STAT_N], v4a_write[V4A_STAT_N], v4a_total[V4A_STAT_N];
static volatile uint32_t v4a_sink;

static bool IRAM_ATTR v4a_on_sent(i2s_chan_handle_t h, i2s_event_data_t *e, void *u)
{
    (void)h; (void)e; (void)u;
    ++v4a_dma_sent;
    return false;
}
static bool IRAM_ATTR v4a_on_qovf(i2s_chan_handle_t h, i2s_event_data_t *e, void *u)
{
    (void)h; (void)e; (void)u;
    ++v4a_dma_qovf;
    return false;
}
static void v4a_sort(uint32_t *a)
{
    for (uint32_t gap = V4A_STAT_N >> 1; gap; gap >>= 1)
        for (uint32_t i = gap; i < V4A_STAT_N; ++i) {
            uint32_t v = a[i], j = i;
            while (j >= gap && a[j-gap] > v) { a[j] = a[j-gap]; j -= gap; }
            a[j] = v;
        }
}
static uint32_t v4a_pct(const uint32_t *a, uint32_t pct)
{
    return a[(V4A_STAT_N * pct + 99u) / 100u - 1u];
}
static uint32_t v4a_avg(const uint32_t *a)
{
    uint64_t sum = 0;
    for (uint32_t i = 0; i < V4A_STAT_N; ++i) sum += a[i];
    return (uint32_t)(sum / V4A_STAT_N);
}
static i2s_chan_handle_t v4a_start(uint32_t *produced)
{
    i2s_chan_handle_t tx = NULL;
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan.dma_desc_num = V4A_DMA_DESC;
    chan.dma_frame_num = V4A_QUANTUM_FRAMES;
    chan.auto_clear_after_cb = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan, &tx, NULL));
    i2s_std_config_t cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(V4A_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = CONFIG_FM6_V4A_I2S_BCLK_GPIO,
            .ws = CONFIG_FM6_V4A_I2S_WS_GPIO,
            .dout = CONFIG_FM6_V4A_I2S_DOUT_GPIO,
            .din = I2S_GPIO_UNUSED,
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx, &cfg));
    const i2s_event_callbacks_t cbs = {.on_sent=v4a_on_sent, .on_send_q_ovf=v4a_on_qovf};
    ESP_ERROR_CHECK(i2s_channel_register_event_callback(tx, &cbs, NULL));
    memset(v4a_stereo_quantum, 0, sizeof v4a_stereo_quantum);
    *produced = 0;
    for (uint32_t i = 0; i < V4A_DMA_DESC; ++i) {
        size_t loaded = 0;
        ESP_ERROR_CHECK(i2s_channel_preload_data(tx, v4a_stereo_quantum, sizeof v4a_stereo_quantum, &loaded));
        ESP_ERROR_CHECK(loaded == sizeof v4a_stereo_quantum ? ESP_OK : ESP_FAIL);
        ++*produced;
    }
    ESP_ERROR_CHECK(i2s_channel_enable(tx));
    return tx;
}
static void v4a_stop(i2s_chan_handle_t tx)
{
    ESP_ERROR_CHECK(i2s_channel_disable(tx));
    ESP_ERROR_CHECK(i2s_del_channel(tx));
}

/* Conservative observation, NOT an exact hardware underrun/sample counter.
 * Check both sides of the potentially blocking submission. The old check only
 * before write could miss a boundary crossed inside the driver. A callback
 * just before producer bookkeeping can conservatively reject a good case.
 * After any observed starvation the case fails; recovery is not benchmarked.
 */
static int v4a_submit(i2s_chan_handle_t tx, uint32_t *produced, uint32_t *starve)
{
    int dry = (int32_t)(*produced - v4a_dma_sent) <= 0;
    size_t written = 0;
    esp_err_t err = i2s_channel_write(tx, v4a_stereo_quantum, sizeof v4a_stereo_quantum, &written, 100);
    dry |= (int32_t)(*produced - v4a_dma_sent) <= 0;
    *starve += dry != 0;
    if (err != ESP_OK || written != sizeof v4a_stereo_quantum) return 0;
    ++*produced;
    return 1;
}
static int v4a_run_case(uint32_t voices)
{
    uint32_t deadline = (uint32_t)(((uint64_t)V4A_CPU_HZ * V4A_QUANTUM_FRAMES + V4A_SAMPLE_RATE-1u) / V4A_SAMPLE_RATE);
    uint32_t produced = 0, warm_errors = 0, warm_starve = 0, starve = 0;
    uint32_t dsp_miss = 0, total_miss = 0;
    bench_init_voices(voices);
    v4a_sink = 0x7f4a7c15u ^ voices;
    v4a_dma_sent = v4a_dma_qovf = 0;
    i2s_chan_handle_t tx = v4a_start(&produced);
    for (uint32_t i=0; i<V4A_WARM_QUANTA; ++i) {
        v4a_sink ^= v4a_render_quantum(voices);
        if (!v4a_submit(tx, &produced, &warm_starve)) {
            v4a_stop(tx);
            printf("FM6-V4A FAIL voices=%u reason=warm-write\n", (unsigned)voices);
            return 0;
        }
    }
    warm_errors = warm_starve + v4a_dma_qovf;
    uint32_t sent0=v4a_dma_sent, qovf0=v4a_dma_qovf;
    for (uint32_t i=0; i<V4A_STAT_N; ++i) {
        uint32_t a=esp_cpu_get_cycle_count();
        uint32_t sounded=v4a_render_quantum(voices);
        uint32_t b=esp_cpu_get_cycle_count();
        int ok=v4a_submit(tx, &produced, &starve);
        uint32_t c=esp_cpu_get_cycle_count();
        if (!ok) {
            v4a_stop(tx);
            printf("FM6-V4A FAIL voices=%u reason=write-timeout-or-short\n", (unsigned)voices);
            return 0;
        }
        v4a_dsp[i]=b-a; v4a_write[i]=c-b; v4a_total[i]=c-a;
        dsp_miss += v4a_dsp[i]>deadline;
        total_miss += v4a_total[i]>deadline;
        v4a_sink=(v4a_sink<<5)^sounded^(uint32_t)v4a_stereo_quantum[(i*2u)%(V4A_QUANTUM_FRAMES*2u)];
    }
    uint32_t sent=v4a_dma_sent-sent0, qovf=v4a_dma_qovf-qovf0;
    v4a_stop(tx);
    v4a_sort(v4a_dsp); v4a_sort(v4a_write); v4a_sort(v4a_total);
    printf("FM6-V4A voices=%u quanta=%u samples=%u deadline_ticks=%u "
           "dsp_avg=%u dsp_p95=%u dsp_p99=%u dsp_max=%u dsp_miss=%u "
           "write_avg=%u write_p99=%u write_max=%u total_avg=%u total_p99=%u total_max=%u total_miss=%u "
           "starve=%u write_fail=0 dma_sent=%u send_q_ovf=%u warm_errors=%u sink=%08x\n",
           (unsigned)voices,(unsigned)V4A_STAT_N,(unsigned)(V4A_STAT_N*V4A_QUANTUM_FRAMES),(unsigned)deadline,
           (unsigned)v4a_avg(v4a_dsp),(unsigned)v4a_pct(v4a_dsp,95),(unsigned)v4a_pct(v4a_dsp,99),
           (unsigned)v4a_dsp[V4A_STAT_N-1],(unsigned)dsp_miss,
           (unsigned)v4a_avg(v4a_write),(unsigned)v4a_pct(v4a_write,99),(unsigned)v4a_write[V4A_STAT_N-1],
           (unsigned)v4a_avg(v4a_total),(unsigned)v4a_pct(v4a_total,99),(unsigned)v4a_total[V4A_STAT_N-1],
           (unsigned)total_miss,(unsigned)starve,(unsigned)sent,(unsigned)qovf,(unsigned)warm_errors,(unsigned)v4a_sink);
    fflush(stdout);
    return 1;
}
static void v4a_task(void *arg)
{
    (void)arg;
    puts("FM6-V4A ARM delay_ms=5000"); fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(5000));
    printf("FM6-V4A platform=ESP32-S3-I2S clock_hz=%u cpu_hz=%u dsp_nominal_sr=%u "
           "ctl=%u quantum_frames=%u dma_desc=2 dma_frame=%u core=1 "
           "bclk_gpio=%d ws_gpio=%d dout_gpio=%d\n",
           (unsigned)V4A_CPU_HZ,(unsigned)V4A_CPU_HZ,(unsigned)V4A_SAMPLE_RATE,
           (unsigned)CTL,(unsigned)V4A_QUANTUM_FRAMES,(unsigned)V4A_QUANTUM_FRAMES,
           CONFIG_FM6_V4A_I2S_BCLK_GPIO,CONFIG_FM6_V4A_I2S_WS_GPIO,CONFIG_FM6_V4A_I2S_DOUT_GPIO);
    fflush(stdout);
    int ok=1;
    for (uint32_t i=0;i<V4A_CASES && ok;++i) ok=v4a_run_case(v4a_voice_cases[i]);
    if (ok) puts("FM6-V4A DONE");
    fflush(stdout);
    vTaskDelete(NULL);
}
void app_main(void)
{
    BaseType_t ok=xTaskCreatePinnedToCore(v4a_task,"fm6_v4a",8192,NULL,20,NULL,1);
    ESP_ERROR_CHECK(ok==pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
