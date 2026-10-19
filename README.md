# fm6_bench

Cross-platform benchmark for the Felucca/MSFA 6-operator FM core on AC7911B8 and ESP32-S3.

The benchmark pins an upstream Felucca revision and validates identical fixed-point rendering before comparing target builds.

- `RESULTS_V2.md` — operator-kernel experiments and target static-disassembly comparison.
- `RESULTS_V3.md` — common target runtime timing harness, ESP32-S3 QEMU/flash image flow, and FM-1/Felucca `fm6bench` console overlay for physical AC7911B8 measurements.
- `HARDWARE_RUN.md` — repeatable physical ESP32-S3 and FM-1 V3 capture flow using `scripts/run_v3_hardware.py`.
- `RESULTS_V4A.md` — transport-aware FM6 baseline using the same 128-frame audio service quantum on ESP32-S3 I2S DMA and FM-1 ALNK0 DMA, with `scripts/run_v4a_hardware.py` for physical capture.

Hosted and QEMU numbers are never treated as physical MCU performance. V3 establishes isolated FM6 cost; V4A adds real audio transport. Final advertised instrument polyphony still requires physical-board logs and the later V4B product-stack stress test.
