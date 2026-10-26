# V4A validation status and physical-run boundary

This file supersedes any earlier implication that V4A green CI means validated hardware polyphony. It does not.

## What is tested

- Python regression tests use synthetic fixtures, explicitly labelled `source=fixture`.
- The ESP32-S3 image is compiled and linked using ESP-IDF 5.5; V3 QEMU remains a separate functional gate.
- AC overlay object smoke tests remain, plus a new build of the actual patched Felucca unity application through the upstream linker and layout checks. No SDK boot blobs or installable `.fwsc` package are included in the AC artifact.
- There are no physical S3/FM-1 performance logs in this change.

## Stricter result handling

The parser now rejects missing/duplicate/reordered rows, concatenated sessions, zero or inconsistent deadlines, negative/invalid counters, impossible percentile ordering and inconsistent miss/max pairs. It requires actual DMA progress within the two-buffer snapshot tolerance. On AC, `total_miss` also fails the gate: the one-bit half selector alone can alias after an even number of missed halves.

`SAFE-MAX` is the highest consecutively passing **tested tier**, not an extrapolated upper bound and not guaranteed product polyphony. A low failed tier cannot be skipped just because a higher tier happens to pass. `--source physical` is an operator declaration, not cryptographic hardware attestation. A label such as `real-s3` does not prove provenance.

The 256 measured 128-frame quanta are only about 0.74 seconds per tier. Six tiers cover one synthetic Algorithm-32 sustain patch. Longer runs, all algorithms, envelopes/release bursts, FX, MIDI/UI/storage/radio stress and captured audio integrity still remain before a product claim.

## S3 timing and starvation

The worker and I2S allocation are pinned to core 1, priority 20. Dynamic CPU frequency scaling is rejected at compile time for this CCOUNT baseline. I2S submission is blocking; `write_*` and `total_*` include waits and must not be reported as CPU busy time. `dsp_*` includes render, mix and staging-buffer pack, but NOT all I2S-driver/ISR work.

The producer/EOF comparison is a conservative **starvation observation**, not an exact sample-underrun counter. It is sampled before and after the write, and warm-up errors are retained. A callback between the completed copy and producer bookkeeping can conservatively fail a borderline case. Zero observations cannot prove bit-perfect DMA output; a later loopback/sequence-tag test is needed. AC uses its half-selector and total-ISR timing, so the two starvation counts are not numerically interchangeable.

Compiler settings also differ: S3 uses `-O2`; the full Felucca application preserves its normal `-Os` plus cold-function minsize pipeline. These are deployed software configurations, not an isolated ISA/CPI comparison. Use V3 for the raw workload and record exact builds for every physical comparison.

## ESP32-S3 package

The artifact includes a merged 4-MiB `flash_image.bin`, tools, ELF/map, config, commit IDs and SHA256SUMS. This is a standalone destructive benchmark image: flashing at offset 0 replaces the existing firmware/partitions in the first 4 MiB. Back up anything needed first. It does not burn eFuses or change firmware-security settings.

Default output pins are BCLK=GPIO4, WS=GPIO5, DOUT=GPIO6. Verify they are free on the board before flashing; a DAC is not required for the timing test. The image does not initialize a codec. Native USB and UART console routing depend on the saved sdkconfig; use the console port, not an assumed device name.

From the extracted artifact directory:

```bash
python3 -m pip install pyserial esptool
# Linux: sha256sum -c SHA256SUMS
# macOS: shasum -a 256 -c SHA256SUMS
python3 tools/run_v4a_hardware.py s3 --port YOUR_SERIAL_PORT --image flash_image.bin --log s3-v4a-run1.log
```

For a subsequent capture with `--no-flash`, reset the board after the runner opens the serial port. Keep the full log and BUILD_COMMIT.txt together. The runner does not measure anything until it is actually connected to a physical board.

## FM-1

The AC CI artifact is an ELF/link-validation artifact, not a USB-upgrade file. Build a `.fwsc` using the pinned Felucca packager and its exact required JieLi SDK components before installation. Never use esptool on the FM-1.

After installation of a corresponding development package, the serial command is `fm6audiobench`. No installation or physical run is claimed by CI.
