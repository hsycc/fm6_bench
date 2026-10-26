# fm6_bench

Cross-platform benchmark for the pinned Felucca/MSFA 6-operator FM core on AC7911B8 and ESP32-S3.

- **`V4A_VALIDATION.md`** — current validation limits, stricter result gates, full-application build coverage and safe physical-run instructions. Read this first.
- `RESULTS_V2.md` — operator-kernel experiments and target static disassembly; instruction counts are not cycles.
- `RESULTS_V3.md` — isolated runtime harness, S3 QEMU/flash image and FM-1 `fm6bench` overlay.
- `HARDWARE_RUN.md` — V3 physical capture helper.
- `RESULTS_V4A.md` — transport-baseline design; the validation addendum above supersedes its earlier starvation/capacity claims.

V4A CI now includes strict Python parser/capture regressions, real ESP-IDF compilation/linking, and the actual patched Felucca unity application's link/layout checks (not just overlay objects). Test fixtures and QEMU are never MCU throughput evidence. Physical logs, longer stress tests and audio-integrity checks are still required before claiming product polyphony.

Felucca's large benchmark voice/output buffers are placed in its existing `.pool` region, which is the same internal SRAM as `.bss`. Neither the linker memory limits nor its minimum-headroom checks are relaxed. The FM math, patch and voice sequence remain unchanged.
