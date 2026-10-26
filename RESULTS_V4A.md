# FM6 benchmark V4A — real audio transport baseline

V4A adds the piece V3 intentionally excluded: a continuously running physical audio transport.

V3 answers:

> How expensive is the shared FM6 workload itself?

V4A answers:

> With the same FM6 workload, mix/pack work and a real double-buffered audio transport, can the target continuously prepare each audio quantum before DMA needs it?

V4A still does **not** claim final instrument polyphony. Product-stack pressure such as global FX, USB audio, UI rendering, MIDI/event bursts and storage work belongs to V4B.

## Fair transport quantum

Felucca/FM-1's real ALNK0 interrupt services one **128-frame half-buffer**. Its engine control block is `CTL = 32`, so each DMA half contains four FM render blocks.

V4A therefore uses the same transport cadence on both targets:

```text
128-frame DMA service quantum
  ├─ FM block 0: 32 samples
  ├─ FM block 1: 32 samples
  ├─ FM block 2: 32 samples
  └─ FM block 3: 32 samples
```

The measurement set is:

- voices: 1 / 8 / 12 / 16 / 20 / 24;
- warm-up: 32 quanta = 4096 samples;
- measured: 256 quanta = 32768 samples;
- FM6 tables/DSP nominal sample rate: 44,100 Hz;
- two transport buffers/halves where the platform allows the same structure.

The total warm-up and measured sample counts intentionally match V3.

## Sample-clock detail

The shared Felucca FM6 tables and DSP are generated for 44,100 Hz.

ESP32-S3 V4A configures its I2S transport for 44,100 Hz.

FM-1's actual ALNK0 hardware frame clock is derived from 24 MHz with 544 timer ticks per frame:

```text
24,000,000 / 544 = 44,117.647... Hz
```

V4A does not hide that hardware difference. The AC7911B8 deadline uses the actual 544-tick frame period, so its transport is naturally about 0.04% tighter than nominal 44.1 kHz. The DSP workload itself remains the same 44.1-kHz-generated FM6 implementation on both targets.

## Common post-FM workload

For every 32-sample FM block V4A:

1. renders the complete shared V3 FM6 voices;
2. sums all voice outputs per sample using a wide accumulator;
3. divides by active voice count only to prevent benchmark arithmetic clipping;
4. duplicates the mono result into 32-bit stereo L/R.

This deterministic mix/pack work is counted inside `dsp_*` on both targets.

It is intentionally simpler than the final product mixer and FX chain. The point of V4A is to add the transport without immediately introducing unequal platform feature stacks.

## Timing units

All V4A row fields use **platform-native timer ticks**.

### ESP32-S3

- timer: per-core CCOUNT;
- timer rate: configured CPU clock, currently 240 MHz;
- `dsp_*` values therefore happen to be CPU cycles.

At 44.1 kHz and 128 frames the nominal deadline is approximately:

```text
ceil(240,000,000 × 128 / 44,100) = 696,599 CCOUNT ticks
```

### AC7911B8 / FM-1

- timer: Felucca's hardware-verified TIMER4;
- timer rate: 24 MHz;
- actual 128-frame ALNK0 half deadline:

```text
128 × 544 = 69,632 TIMER4 ticks
```

No assumed 320 MHz AC CPU clock is used or required.

For this reason the standardized field is `deadline_ticks`, not `deadline_cycles`.

## Metrics

Each voice-count row reports:

| Metric | Meaning |
|---|---|
| `dsp_avg/p95/p99/max` | FM6 render + deterministic mix/pack time |
| `dsp_miss` | DSP phase alone exceeded one 128-frame transport deadline |
| `write_avg/p99/max` | transport handoff cost; S3 = I2S write/wait, AC = copy into real ALNK0 DMA half |
| `total_avg/p99/max` | whole service-side timing diagnostic |
| `total_miss` | whole measured service interval exceeded one deadline |
| `starve` | transport had no replacement quantum ready in time |
| `write_fail` | transport submission failed |
| `send_q_ovf` | S3 I2S send-event queue overflow; AC has no equivalent queue and reports 0 |

`total_p99` is diagnostic rather than the primary S3 capacity gate because a blocking `i2s_channel_write()` legitimately includes time waiting for a DMA slot to become available.

## Starvation semantics

The two platforms expose different low-level mechanisms but the benchmark maps them to the same product-level meaning.

### ESP32-S3

V4A counts how many complete 128-frame quanta have been supplied to I2S and how many DMA `on_sent` callbacks have consumed. After rendering the next quantum, but before submitting it, if DMA has already consumed every quantum supplied by the producer, `starve` increments.

`on_send_q_ovf` is tracked separately. It is **not** treated as an underrun.

### AC7911B8

V4A runs inside the real Felucca ALNK0 ISR. After rendering and acknowledging the half-buffer it uses Felucca's existing hardware condition:

```text
fm1_audio_free_half() != half
```

This is the same condition normally recorded as `felucca_dbg.late`: DMA advanced to another half before rendering completed. V4A maps that count to `starve`.

## Safety gate

The default V4A raw-audio safety gate is:

```text
dsp_p99 <= 85% of the real transport deadline
AND dsp_miss == 0
AND starve == 0
AND write_fail == 0
AND send_q_ovf == 0
```

This deliberately requires both computational margin and uninterrupted audio delivery.

A parser fixture in CI also verifies the negative case: a 24-voice result that otherwise has acceptable p99 timing but contains `starve=1` must not pass as 24 safe voices.

## ESP32-S3 implementation

`targets/esp32s3_v4a` builds a physical ESP-IDF 5.5 target using real standard-mode I2S TX DMA:

- 44.1 kHz;
- stereo;
- 32-bit slots;
- 128 DMA frames;
- two DMA descriptors;
- `on_sent` and `on_send_q_ovf` callbacks;
- configurable BCLK / WS / DOUT pins.

A codec does not need to be connected for the timing benchmark; the I2S peripheral still runs from the real hardware clock and DMA engine.

CI compiles and links this target and produces a merged 4 MiB `flash_image.bin`. V4A intentionally does not use QEMU as an I2S-DMA performance source.

## AC7911B8 / Felucca implementation

Apply the overlay to the pinned Felucca checkout:

```bash
python3 scripts/apply_felucca_v4a_overlay.py /path/to/Felucca
```

This retains the V3 `fm6bench` command and adds:

```text
fm6audiobench
```

During a V4A case, the real ALNK0 ISR latches benchmark mode for the complete half, renders four shared 32-sample FM blocks into the real DMA half, records TIMER4 timing and real late-half events, then returns to Felucca's normal audio path after the case.

When V4A is inactive, Felucca keeps its original ordering of voice shedding, CPU-load IIR, late detection and debug accounting.

V4A bypasses ordinary track mixing, global FX and UAC tap during benchmark halves. Those unequal product-stack costs belong to V4B.

## Physical runner

Install host tools once:

```bash
python3 -m pip install pyserial esptool
```

### S3

Extract the `v4a-esp32s3-i2s-image` artifact and run:

```bash
python3 scripts/run_v4a_hardware.py s3 \
  --port /dev/cu.usbmodem101 \
  --image flash_image.bin
```

### FM-1

After installing the V4A Felucca development firmware:

```bash
python3 scripts/run_v4a_hardware.py fm1 \
  --port /dev/cu.usbmodem1101
```

The runner sends `fm6audiobench`, captures all six rows through `FM6-V4A DONE`, validates completeness, and invokes `parse_v4a_audio.py`.

## What V4A will decide

Once both physical logs exist, V4A gives the first transport-aware answer to:

> Is 12, 16, 20 or 24 voices a defensible baseline before product FX/UI/USB load is added?

The final advertised polyphony still waits for V4B product-stack stress testing.
