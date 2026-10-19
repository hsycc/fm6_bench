/* Included by patched console.c before con_exec(), after con_put* helpers. */
static void con_fm6bench_u32(const char *k, uint32_t v)
{
    con_puts(k);
    con_putc('=');
    con_dec((int32_t)v);
    con_putc(' ');
}

static void con_fm6bench(void)
{
    uint32_t i;

    con_puts("FM6-V3 notice=audio-muted-during-benchmark\r\n");

    /* Keep TIMER5/USB alive between samples, but prevent the audio ISR from
     * running while the standalone workload is timed. audio_init() reattaches
     * ALNK0 afterwards. */
    fm1_irq_off();
    fm1_audio_stop();
    fm1_irq_mask(FM1_IRQ_ALNK0);
    fm1_irq_on();

    fm6_bench_ac_felucca_run();

    fm1_irq_off();
    audio_init();
    fm1_irq_on();

    con_puts("FM6-V3 platform=AC7911B8-Felucca-TIMER4 clock_hz=24000000 cpu_hz=0 sr=44100 block=");
    con_dec((int32_t)CTL);
    con_puts(" samples=");
    con_dec((int32_t)BENCH_RT_SAMPLES);
    con_puts("\r\n");

    for (i = 0; i < BENCH_RT_CASES; ++i) {
        const bench_rt_result_t *r = &bench_rt_results[i];
        con_puts("FM6-V3 ");
        con_fm6bench_u32("voices", r->voices);
        con_fm6bench_u32("overhead", r->read_overhead);
        con_fm6bench_u32("deadline", r->deadline_ticks);
        con_fm6bench_u32("min", r->min_ticks);
        con_fm6bench_u32("avg", r->avg_ticks);
        con_fm6bench_u32("p50", r->p50_ticks);
        con_fm6bench_u32("p95", r->p95_ticks);
        con_fm6bench_u32("p99", r->p99_ticks);
        con_fm6bench_u32("max", r->max_ticks);
        con_fm6bench_u32("miss", r->misses);
        con_fm6bench_u32("util_permille", r->util_permille);
        con_fm6bench_u32("equiv_avg_cycles", r->avg_equiv_cycles);
        con_fm6bench_u32("equiv_deadline_cycles", r->deadline_equiv_cycles);
        con_puts("sink=");
        con_hex(r->sink, 8);
        con_puts("\r\n");
    }
    con_puts("FM6-V3 DONE\r\n");
}
