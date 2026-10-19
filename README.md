# fm6_bench

Cross-platform benchmark for the Felucca/MSFA 6-operator FM core on AC7911B8 and ESP32-S3.

The benchmark pins an upstream Felucca revision and validates identical fixed-point rendering before comparing target builds.

- `RESULTS_V2.md` — operator-kernel experiments and target static-disassembly comparison.
- `RESULTS_V3.md` — common target runtime timing harness, ESP32-S3 QEMU/flash image flow, and FM-1/Felucca `fm6bench` console overlay for physical AC7911B8 measurements.

V3 deliberately keeps hosted and QEMU numbers separate from physical MCU results. Stable product polyphony is not claimed until real-board timing and full audio-path tests exist.
