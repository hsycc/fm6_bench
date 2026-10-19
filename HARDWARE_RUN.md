# Physical FM6 benchmark runbook

This runbook turns V3's physical-board procedure into repeatable commands. It does **not** change the FM6 workload or the V3 pass criterion.

## What counts as a physical result

A valid physical result must come from a real ESP32-S3 or AC7911B8/FM-1 board and contain:

- one `FM6-V3 platform=...` header;
- result rows for 1, 8, 12, 16, 20 and 24 voices;
- `FM6-V3 DONE`.

The helper refuses incomplete captures before invoking `parse_v3_runtime.py`.

Install host-side tools once:

```bash
python3 -m pip install pyserial esptool
```

## ESP32-S3

Download the `v3-esp32s3-qemu-results` artifact from the latest successful `main` workflow and extract it. The artifact contains a merged 4 MiB `flash_image.bin` that is written at offset 0.

Typical macOS/Linux run:

```bash
python3 scripts/run_v3_hardware.py s3 \
  --port /dev/cu.usbmodem101 \
  --image flash_image.bin
```

Typical Windows run:

```powershell
python scripts/run_v3_hardware.py s3 --port COM6 --image flash_image.bin
```

The helper performs:

```text
esptool write_flash 0x0 flash_image.bin
        ↓
wait for USB serial reconnect
        ↓
capture boot / FM6-V3 rows
        ↓
require FM6-V3 DONE + all six voice rows
        ↓
save build/real-s3.log
        ↓
parse_v3_runtime.py --label real-s3
```

If the correct image is already installed:

```bash
python3 scripts/run_v3_hardware.py s3 --port /dev/cu.usbmodem101 --no-flash
```

Use a slower flash baud if a USB bridge is unreliable:

```bash
python3 scripts/run_v3_hardware.py s3 \
  --port /dev/cu.usbmodem101 \
  --image flash_image.bin \
  --flash-baud 460800
```

## FM-1 / AC7911B8

First prepare and install the V3 Felucca development firmware as described in `RESULTS_V3.md`:

```bash
python3 scripts/apply_felucca_v3_overlay.py /path/to/Felucca
```

After the board boots and its CDC console is visible:

```bash
python3 scripts/run_v3_hardware.py fm1 --port /dev/cu.usbmodem1101
```

The helper opens the console, sends:

```text
fm6bench
```

and captures through `FM6-V3 DONE`. The raw log is saved as `build/real-ac7911b8.log` and parsed with label `real-ac7911b8`.

If `fm6bench` was triggered manually, listen without sending a command:

```bash
python3 scripts/run_v3_hardware.py fm1 \
  --port /dev/cu.usbmodem1101 \
  --no-trigger
```

## Safety margin

The default physical-data gate remains:

```text
p99 <= 85% of the 32-sample deadline
AND
misses == 0
```

Override only when intentionally evaluating another product margin:

```bash
python3 scripts/run_v3_hardware.py s3 \
  --port /dev/cu.usbmodem101 \
  --image flash_image.bin \
  --safe-percent 80
```

## What the first two real logs answer

Once `real-s3.log` and `real-ac7911b8.log` exist, the comparison is finally based on the same FM6 source, patch, 44.1 kHz rate, 32-sample block, voice counts and percentile rule on both physical SoCs.

That is sufficient to choose a **raw-FM DSP target** among 12 / 16 / 20 / 24 voices. It is still not the final product polyphony guarantee.

The next stage is a full-system audio-path test that includes audio ISR/I2S DMA servicing, mixer/FX, MIDI/UI/USB load and actual underrun counters. V3 intentionally isolates the FM render call, so its result represents synthesis headroom rather than total-instrument CPU load.

## CI dry-run

No hardware is needed to exercise the runner's command construction and argument parsing:

```bash
python3 scripts/run_v3_hardware.py s3 --dry-run --port TEST --image flash_image.bin
python3 scripts/run_v3_hardware.py fm1 --dry-run --port TEST
```

These dry-runs are part of CI and do not install or access pyserial/esptool.
