# V2 results: FM-1-style operator lookup and chain fusion

This document records the reproducible V2 results for the FM6 benchmark.

## Scope and pinned inputs

- Felucca source: `5738617968a22b5d4c0d3470cfcc95559052f446`
- Workload: six-operator FM6, 44.1 kHz tables/control assumptions from the pinned source, 32-sample benchmark blocks, heavy algorithm-32 patch with feedback 7.
- Host runtime: GitHub hosted x86 runner. Host timings are useful for relative regression only; they are **not AC7911B8 or ESP32-S3 performance**.
- AC target: JieLi pi32v2 compiler/toolchain downloaded by Felucca's pinned `get_toolchain.sh`.
- ESP32-S3 target: ESP-IDF 5.5 Xtensa GCC 14.2.0.

## What is modeled from the stock FM-1

Reverse engineering of the stock FM-1 reports:

- hot FM render code copied to RAM;
- a dedicated `fm_op_chain3_kernel`;
- operator sine lookup through a quadrant-folded RAM table;
- operator phase indexing equivalent to 1024 steps per quadrant (`phase >> 12`), about 4096 discrete positions per full cycle;
- no Felucca-style linear interpolation in the stock operator path.

The V2 `stock-indexing-like` experiment therefore uses a 1025-entry Q15 quarter-wave table with quadrant folding and 4096 full-cycle phase positions, with no interpolation.

**Important:** this is not a byte-exact reconstruction of the stock FM-1 kernel. The reverse-engineered firmware also uses its own sine/exp2 LUT representation and arithmetic. V2 reproduces the observed indexing/resolution and no-interpolation property, not the exact factory math.

## Superseded experiment

An earlier V2 attempt simply removed interpolation from Felucca's 1024-point full-wave table. It showed much larger speedups, but also reduced sine phase resolution by roughly 4x relative to the stock FM-1 indexing. Those numbers are intentionally superseded and must not be used as evidence for the stock implementation.

## Host full-engine result

Latest all-green CI run (`39163363429`) measured the stock-indexing-like variant against the unmodified Felucca baseline at `-O2`:

| Voices | Baseline avg/block | Stock-indexing-like avg/block | Relative speedup |
|---:|---:|---:|---:|
| 12 | 5911 ns | 5546 ns | 1.0658x |
| 16 | 7882 ns | 7381 ns | 1.0679x |
| 20 | 9817 ns | 9170 ns | 1.0706x |
| 24 | 11779 ns | 11223 ns | 1.0495x |

Other hosted runs of the same implementation moved within normal runner noise; the useful summary is approximately **5–8% relative x86 full-engine improvement**. This does not predict the percentage gain on either MCU.

The baseline and no-interpolation variants each produce deterministic CRCs across `-O2` and `-Os`. Their CRCs differ from each other by design because the sine lookup changed.

## Local kernel fidelity

A non-recursive three-operator cascade comparison reports:

- fused interpolated chain == generic interpolated chain: **bit-exact PASS**;
- fused stock-indexing-like chain == generic stock-indexing-like chain: **bit-exact PASS**;
- stock-indexing-like vs interpolated local chain: **54.143 dB time-aligned SNR**;
- RMS signal: `7,905,936.757`;
- RMS error: `15,516.612`;
- mean absolute error: `9,749.806`;
- max absolute error: `92,327`.

The full heavy feedback-FM trajectory falls to about 4.672 dB time-aligned SNR after long rendering. That is **not a listening-quality score**. With feedback, small numerical differences re-enter later phase/modulation and make two deterministic trajectories rapidly decorrelate. It proves the implementations are not bit-identical, not that one sounds 4.7 dB-SNR noisy.

## Target hot-loop static analysis

`analyze_hot_loops.py` counts compiled instruction **lines inside the per-sample loop** and reports helper calls separately. These are static regression metrics, **not CPU cycles**. They do not include cache/memory stalls, instruction latency, branch costs, setup outside the loop, or real audio scheduling overhead.

### AC7911B8 / pi32v2

The result is identical at `-O2` and `-Os`; all listed loops contain `0 calls/sample`.

| Three-op path | Static loop lines/sample | Calls/sample | Relative to generic interpolated |
|---|---:|---:|---:|
| Generic interpolated | 52 | 0 | 1.000x |
| Generic stock-indexing-like | 52 | 0 | 1.000x |
| Fused interpolated | 56 | 0 | 0.929x |
| Fused stock-indexing-like | **49** | 0 | **1.061x** |

Per-operator replace-loop components:

- interpolated modulated: 18 lines;
- interpolated pure: 16;
- stock-indexing-like modulated: 18;
- stock-indexing-like pure: 16.

Thus V2 does **not** show a benefit from the lookup change alone on pi32v2. The best tested combination is fused + stock-indexing-like, reducing this static three-op caller-loop count from 52 to 49, about 5.8% fewer lines / a 1.061x line-count ratio.

A previously quoted manual count of ~26 for the AC fused stock-indexing-like loop was an undercount caused by misreading pi32v2 disassembly representation. The reproducible CI result is **49**, and only the CI-script value should be used.

### ESP32-S3 / Xtensa LX7, `-O2`

At `-O2` all four paths are fully inlined in these loops (`0 calls/sample`), making this the cleanest S3 static comparison.

| Three-op path | Static loop lines/sample | Calls/sample | Relative to generic interpolated |
|---|---:|---:|---:|
| Generic interpolated | 66 | 0 | 1.000x |
| Generic stock-indexing-like | 71 | 0 | 0.930x |
| Fused interpolated | **62** | 0 | **1.065x** |
| Fused stock-indexing-like | 64 | 0 | 1.031x |

So for this C formulation and compiler, S3 `-O2` prefers **chain fusion while keeping interpolation**. The stock-indexing-like quadrant-fold lookup costs more static caller-loop lines when fully inlined.

### ESP32-S3 / `-Os`

At `-Os`:

| Three-op path | Caller-loop lines/sample | Calls/sample |
|---|---:|---:|
| Generic interpolated | 62 | 3 |
| Generic stock-indexing-like | 62 | 3 |
| Fused interpolated | 43 | 3 |
| Fused stock-indexing-like | 43 | 3 |

The apparent 62 -> 43 reduction **must not be read as a 31% cycle reduction**, because all four paths still make three outlined helper calls per sample. The callee work is outside the counted caller loop. `-Os` is therefore useful here for code-layout/size observations, not for a clean arithmetic-work comparison.

## Target code-generation observations

At S3 `-O2`, the full no-interpolation operator is larger than baseline:

- baseline `fm6_op_run`: 118 instructions / 312 B;
- stock-indexing-like full variant: about 129 instructions / 346 B (the compiler may alias identical generated functions).

At AC `-O2/-Os`, the full no-interpolation operator body is smaller in static function size:

- baseline `fm6_op_run`: 104 instructions / 318 B;
- stock-indexing-like: 95 instructions / 294 B.

Function-body size and per-sample dynamic loop work are different metrics; the AC loop count itself remains 52 -> 52 until chain fusion is applied.

## What V2 proves

1. The naive 1K no-interpolation result was not a fair stock-FM-1 model and is superseded.
2. A stock-phase-resolution-like 4096-position no-interpolation lookup gives a modest ~5–8% speedup on hosted x86 full-engine tests, not the earlier dramatic gain.
3. On target static `-O2` hot loops, no-interpolation alone is not a clear win:
   - AC: 52 -> 52;
   - S3: 66 -> 71.
4. Chain fusion is architecture/compiler dependent:
   - AC best tested path: 52 -> 49 with fused stock-indexing-like lookup;
   - S3 best tested path: 66 -> 62 with fused interpolated lookup.
5. V2 does **not** demonstrate a special 2x AC FM arithmetic advantage.
6. These target values are static instruction-line metrics, not real cycles and not real CPU utilization.

## Product interpretation

The evidence still favors AC7911B8 for a cost-focused pure-FM product, but for reasons stronger than a speculative magic FM instruction:

- its 320 MHz clock budget vs ESP32-S3's 240 MHz remains material;
- the stock FM-1 proves this SoC family is already used for the exact six-operator product class;
- its audio/Bluetooth integration can reduce surrounding BOM and integration work;
- the reverse-engineered factory firmware shows product-specific kernel and RAM-placement engineering that can guide a native implementation.

However, V2 also shows that the same micro-optimization does not automatically favor AC. ESP32-S3's compiler/ISA responds differently, and its best tested fused path keeps the interpolated lookup.

## What CI still cannot prove

GitHub hosted runners contain neither target board, so V2 cannot honestly report:

- AC7911B8 cycles/voice/sample;
- ESP32-S3 cycles/voice/sample;
- actual 12/16/20/24-voice CPU percentages;
- p95/p99 audio ISR deadline margins;
- cache/IRAM/flash-placement penalties;
- maximum stable polyphony with complete UI, MIDI and FX.

Those require target execution, preferably the same timed harness on a real FM-1/AC7911B8 board and an ESP32-S3 board.

## Next useful benchmark

V3 should move from a Felucca-C approximation to the exact upstream MSFA `FmOpKernel` representation and/or a faithful reconstruction of the stock FM-1 `fm_op_chain3_kernel`, then add self-hosted hardware jobs that report target cycle counters for 12/16/20/24 voices. That is the point where a defensible real polyphony/CPU comparison becomes possible.
