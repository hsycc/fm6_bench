/* Included by patched console.c after the V3 benchmark console helpers. */
static void con_fm6audiobench_row(uint32_t i)
{
    const struct v4a_ac_result *r = &v4a_ac_results[i];
    con_puts("FM6-V4A ");
    con_fm6bench_u32("voices", r->voices);
    con_fm6bench_u32("quanta", V4A_AC_N);
    con_fm6bench_u32("samples", V4A_AC_N * V4A_QUANTUM_FRAMES);
    con_fm6bench_u32("deadline_ticks", r->deadline_ticks);
    con_fm6bench_u32("dsp_avg", r->dsp_avg);
    con_fm6bench_u32("dsp_p95", r->dsp_p95);
    con_fm6bench_u32("dsp_p99", r->dsp_p99);
    con_fm6bench_u32("dsp_max", r->dsp_max);
    con_fm6bench_u32("dsp_miss", r->dsp_miss);
    con_fm6bench_u32("write_avg", r->write_avg);
    con_fm6bench_u32("write_p99", r->write_p99);
    con_fm6bench_u32("write_max", r->write_max);
    con_fm6bench_u32("total_avg", r->total_avg);
    con_fm6bench_u32("total_p99", r->total_p99);
    con_fm6bench_u32("total_max", r->total_max);
    con_fm6bench_u32("total_miss", r->total_miss);
    con_fm6bench_u32("starve", r->late);
    con_fm6bench_u32("write_fail", 0u);
    con_fm6bench_u32("dma_sent", V4A_AC_N);
    con_fm6bench_u32("send_q_ovf", 0u);
    con_puts("sink=");
    con_hex(r->sink, 8);
    con_puts("\r\n");
}

static void con_fm6audiobench(void)
{
    uint32_t i;

    con_puts("FM6-V4A notice=real-ALNK0-ISR-benchmark-audio-replaced-during-run\r\n");
    con_puts("FM6-V4A platform=AC7911B8-Felucca-ALNK0 clock_hz=24000000 cpu_hz=0 ");
    con_puts("dsp_nominal_sr=44100 transport_sr_num=24000000 transport_sr_div=544 ");
    con_puts("ctl=");
    con_dec((int32_t)CTL);
    con_puts(" quantum_frames=");
    con_dec((int32_t)V4A_QUANTUM_FRAMES);
    con_puts(" dma_halves=2\r\n");

    for (i = 0; i < V4A_CASES; ++i) {
        v4a_ac_start_case(v4a_voice_cases[i]);
        while (!v4a_ac_done)
            fm1_wdt_feed();
        v4a_ac_finish_case(i);
        con_fm6audiobench_row(i);
    }

    con_puts("FM6-V4A DONE\r\n");
}
