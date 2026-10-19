# FM6 benchmark V3 — target runtime timing

V3 moves the project from V2's target **static instruction** comparison to a
common runtime timing format that can run on physical AC7911B8 and ESP32-S3
hardware.

## Status

Implemented:

- shared 6-op workload for 1 / 8 / 12 / 16 / 20 / 24 simultaneous voices;
- 128 warm-up blocks + 1024 measured blocks per voice count;
- min / average / p50 / p95 / p99 / max, deadline misses and deadline utilization;
- calibrated counter-read overhead subtraction;
- ESP32-S3 runner using the CPU CCOUNT counter at the configured 240 MHz;
- AC7911B8 runner using Felucca's hardware-verified 24 MHz TIMER4 timebase;
- per-block interrupt isolation around only the FM render call;
- ESP32-S3 QEMU boot/execution gate;
- flashable merged ESP32-S3 `flash_image.bin` in CI artifacts;
- Felucca overlay that adds a CDC console command, `fm6bench`, for physical FM-1 testing;
- log parser with a default engineering gate of p99 <= 85% of the audio deadline and zero misses.

Still required before claiming a physical maximum polyphony:

1. run the ESP32-S3 image on a real S3;
2. build/install the Felucca overlay and run `fm6bench` on a real FM-1 / AC7911B8;
3. after the raw-DSP comparison, run a second full-system test in the real audio/I2S path to include ISR, mixer/FX and underrun behavior.

**No QEMU or hosted-runner number in this document is a physical MCU performance result.**

## Workload

The benchmark renders complete Felucca FM6 voices in blocks of `CTL = 32`
samples at 44.1 kHz. The synthetic patch deliberately keeps all six operators
active and uses:

- algorithm 32 (`FP_ALG = 31`);
- six carriers;
- feedback 7;
- operator output level 99;
- high pitch/amplitude modulation settings;
- notes spread over a 25-note range.

This is a stable stress workload for comparing targets. It is not intended to
represent the average cost of every DX7 patch.

One 32-sample block has a real-time deadline of:

- ESP32-S3 at 240 MHz CCOUNT: `ceil(240,000,000 * 32 / 44,100) = 174,150` cycles;
- AC7911B8 TIMER4 at 24 MHz: `ceil(24,000,000 * 32 / 44,100) = 17,415` timer ticks.

The AC result does not need a known CPU clock to determine real-time headroom:
TIMER4 measures elapsed wall time directly. `cpu_hz` remains zero until the
actual firmware CPU clock is independently confirmed.

## Measurement boundary

For each measured block:

```text
interrupts off / critical section
    counter A
    bench_render_voices(N)      <-- only this is timed
    counter B
interrupts on
sink + watchdog/service hook    <-- outside timing
```

The minimum back-to-back counter-read cost is calibrated separately and
subtracted from every sample.

On the S3, `esp_cpu_get_cycle_count()` provides the per-core cycle counter and
the measured block is wrapped in a FreeRTOS critical section.

On the AC7911B8, TIMER4 runs from the 24 MHz crystal. The benchmark disables
interrupts only for one 32-sample render at a time; between blocks interrupts
run normally. The Felucca console overlay stops/masks ALNK0 audio while the
benchmark is active, feeds the watchdog outside the measured region, then
reinitializes audio before printing results.

## Host harness

The host harness exists to validate the measurement machinery, output format,
percentiles, parser and deterministic workload. It uses
`CLOCK_MONOTONIC_RAW` nanoseconds and therefore must not be compared with MCU
cycles.

A representative GitHub hosted run after moving sink bookkeeping outside the
timed interval was approximately:

| Voices | Average / 32-sample block | p99 | Deadline misses |
|---:|---:|---:|---:|
| 1 | 0.51 us | 0.52 us | 0 |
| 8 | 3.97 us | 5.84 us | 0 |
| 12 | 5.87 us | 6.59 us | 0 |
| 16 | 7.77 us | 7.78 us | 0 |
| 20 | 9.97 us | 14.86 us | 0 |
| 24 | 11.84 us | 15.84 us | 0 |

These values only show that the harness scales as expected on the CI host.

## ESP32-S3 QEMU

QEMU is a **functional target gate**. It proves that:

- the ESP-IDF target image builds and boots;
- the CCOUNT instrumentation executes;
- all six voice-count workloads complete;
- sink/result data survives optimization;
- serial output and the parser agree.

QEMU's reported CCOUNT values are emulator-model values. They are far below
credible physical S3 FM6 costs and **must not be used to estimate polyphony or
CPU load**.

The CI artifact `v3-esp32s3-qemu-results` contains:

- `flash_image.bin` — merged 4 MiB image for easy hardware flashing;
- application, bootloader and partition binaries;
- ELF and map;
- `sdkconfig`;
- QEMU log and parsed summary.

The S3 CI uses ESP-IDF's minimal build mode to keep only `main` and its
transitive dependencies.

## Physical ESP32-S3 run

Either build/flash with ESP-IDF from `targets/esp32s3`, or flash the merged CI
artifact at address 0. Capture the serial output through the line
`FM6-V3 DONE`.

Then parse it:

```bash
python3 scripts/parse_v3_runtime.py --label real-s3 s3.log
```

The parser prints ticks/cycles per sample, p99/max deadline utilization,
misses, and the largest voice count passing the default safety gate.

## Physical FM-1 / AC7911B8 run

The overlay is intentionally separate from upstream Felucca. Start from the
pinned Felucca checkout and apply:

```bash
python3 scripts/apply_felucca_v3_overlay.py /path/to/Felucca
```

Then build/install that development firmware using Felucca's normal build and
installation process. The overlay adds one read-only-style diagnostic action
to the existing CDC console:

```text
fm6bench
```

During the command, speaker/headphone audio is muted, ALNK0 is masked, the six
voice-count cases run, audio is reinitialized, and standardized `FM6-V3`
rows are printed. Capture through `FM6-V3 DONE`, then run:

```bash
python3 scripts/parse_v3_runtime.py --label real-ac7911b8 ac.log
```

The primary AC comparison metric is TIMER4 elapsed time / deadline utilization.
No 320 MHz conversion is assumed by the harness.

## Pass criterion

The default parser criterion is deliberately stricter than simply avoiding an
underrun:

```text
p99 <= 85% of the 32-sample deadline
AND
measured deadline misses == 0
```

Change it with `--safe-percent` if another product margin is desired.

This criterion answers "how many voices leave useful DSP headroom" for the
isolated FM6 workload. It is **not yet the final instrument guarantee**, because
a complete product also spends time on mixer/FX, MIDI/UI/USB, audio ISR work,
and other system tasks.

## What V3 can answer after the two real-board logs exist

For the first time the AC7911B8 and ESP32-S3 comparison will use the same FM6
source, sample rate, patch, block size, voice counts, percentile method and
headroom criterion. The resulting real-hardware table will be the appropriate
basis for deciding whether the pocket FM keyboard should target 12, 16, 20 or
24 guaranteed voices on each SoC.
