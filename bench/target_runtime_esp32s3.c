/* ESP32-S3 V3 runtime benchmark.
 * Uses the per-core CCOUNT exposed by esp_cpu_get_cycle_count(). One measured
 * 32-sample FM6 block is wrapped in a FreeRTOS critical section so scheduler
 * and ordinary interrupts do not become part of the raw DSP cycle number.
 */
#include <stdint.h>
#include <stdio.h>

#include "sdkconfig.h"
#include "esp_cpu.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"

static portMUX_TYPE bench_rt_mux = portMUX_INITIALIZER_UNLOCKED;

#define BENCH_RT_PLATFORM "ESP32-S3-CCOUNT"
#define BENCH_RT_CLOCK_NOW() esp_cpu_get_cycle_count()
#define BENCH_RT_CLOCK_HZ ((uint32_t)CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1000000u)
#define BENCH_RT_CPU_HZ BENCH_RT_CLOCK_HZ
#define BENCH_RT_ENTER() portENTER_CRITICAL(&bench_rt_mux)
#define BENCH_RT_EXIT() portEXIT_CRITICAL(&bench_rt_mux)
#include "target_runtime_core.inc"

void app_main(void)
{
    bench_rt_run_all();
    printf("FM6-V3 platform=%s clock_hz=%u cpu_hz=%u sr=%u block=%u samples=%u\n",
           BENCH_RT_PLATFORM, (unsigned)BENCH_RT_CLOCK_HZ, (unsigned)BENCH_RT_CPU_HZ,
           (unsigned)BENCH_RT_SAMPLE_RATE, (unsigned)CTL, (unsigned)BENCH_RT_SAMPLES);
    for (uint32_t i = 0; i < BENCH_RT_CASES; ++i) {
        const bench_rt_result_t *r = &bench_rt_results[i];
        printf("FM6-V3 voices=%u overhead=%u deadline=%u min=%u avg=%u p50=%u p95=%u p99=%u max=%u "
               "miss=%u util_permille=%u equiv_avg_cycles=%u equiv_deadline_cycles=%u sink=%08x\n",
               (unsigned)r->voices, (unsigned)r->read_overhead, (unsigned)r->deadline_ticks,
               (unsigned)r->min_ticks, (unsigned)r->avg_ticks, (unsigned)r->p50_ticks,
               (unsigned)r->p95_ticks, (unsigned)r->p99_ticks, (unsigned)r->max_ticks,
               (unsigned)r->misses, (unsigned)r->util_permille,
               (unsigned)r->avg_equiv_cycles, (unsigned)r->deadline_equiv_cycles,
               (unsigned)r->sink);
    }
    puts("FM6-V3 DONE");
    fflush(stdout);
    for (;;)
        vTaskDelay(pdMS_TO_TICKS(1000));
}
