#!/usr/bin/env python3
"""Flash/trigger, capture, and summarize FM6 V4A physical audio benchmarks."""
from __future__ import annotations

import argparse
import subprocess
import sys
import time
from pathlib import Path

# Reuse the serial/esptool compatibility helpers already validated by V3.
from run_v3_hardware import detect_esptool_write_command, require_serial, run_checked, wait_open

DONE = "FM6-V4A DONE"
EXPECTED_VOICES = (1, 8, 12, 16, 20, 24)


def validate_log(text: str) -> None:
    if "FM6-V4A platform=" not in text:
        raise SystemExit("capture ended without an FM6-V4A platform header")
    if DONE not in text:
        raise SystemExit("capture ended before FM6-V4A DONE")
    missing = [v for v in EXPECTED_VOICES if f"FM6-V4A voices={v} " not in text]
    if missing:
        raise SystemExit(f"capture is missing V4A voice rows: {missing}")


def capture(*, port: str, baud: int, timeout: float, open_timeout: float,
            log_path: Path, trigger: bytes | None) -> None:
    # Import is intentionally deferred by wait_open so CI dry-run has no
    # pyserial dependency.
    ser = wait_open(port, baud, open_timeout)
    print(f"opened {port} at {baud} baud", flush=True)
    try:
        ser.reset_input_buffer()
        if trigger:
            time.sleep(0.25)
            ser.write(trigger)
            ser.flush()
            print(f"sent trigger: {trigger!r}", flush=True)

        start = time.monotonic()
        lines: list[str] = []
        while time.monotonic() - start < timeout:
            raw = ser.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
            print(line, flush=True)
            lines.append(line)
            if DONE in line:
                break
    finally:
        ser.close()

    text = "\n".join(lines) + "\n"
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_text(text)
    print(f"saved raw capture: {log_path}", flush=True)
    validate_log(text)


def summarize(log_path: Path, label: str, safe_percent: float) -> None:
    parser = Path(__file__).with_name("parse_v4a_audio.py")
    run_checked([
        sys.executable, str(parser), "--label", label,
        "--safe-percent", str(safe_percent), str(log_path),
    ])


def add_common(ap: argparse.ArgumentParser, default_log: str) -> None:
    ap.add_argument("--port", help="serial device, e.g. /dev/cu.usbmodem101 or COM6")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--timeout", type=float, default=45.0,
                    help="seconds to wait for all six V4A cases")
    ap.add_argument("--open-timeout", type=float, default=10.0)
    ap.add_argument("--log", type=Path, default=Path(default_log))
    ap.add_argument("--safe-percent", type=float, default=85.0)
    ap.add_argument("--dry-run", action="store_true")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="target", required=True)

    s3 = sub.add_parser("s3", help="flash/capture the physical ESP32-S3 I2S V4A image")
    add_common(s3, "build/real-s3-v4a.log")
    s3.add_argument("--image", type=Path, default=Path("flash_image.bin"))
    s3.add_argument("--flash-baud", type=int, default=921600)
    s3.add_argument("--no-flash", action="store_true")

    fm1 = sub.add_parser("fm1", help="trigger/capture Felucca's real-ALNK0 V4A command")
    add_common(fm1, "build/real-ac7911b8-v4a.log")
    fm1.add_argument("--no-trigger", action="store_true")

    args = ap.parse_args()

    if args.target == "s3":
        if not args.no_flash and not args.image.is_file() and not args.dry_run:
            raise SystemExit(f"merged image not found: {args.image}")
        if not args.port and not args.dry_run:
            raise SystemExit("--port is required for a physical run")
        if not args.no_flash:
            write_cmd = detect_esptool_write_command(args.dry_run)
            run_checked([
                sys.executable, "-m", "esptool", "--chip", "esp32s3",
                "--port", args.port or "PORT", "--baud", str(args.flash_baud),
                write_cmd, "0x0", str(args.image),
            ], dry_run=args.dry_run)
        if args.dry_run:
            print(f"would capture {args.port or 'PORT'} -> {args.log} and parse as real-s3-v4a")
            return 0
        capture(port=args.port, baud=args.baud, timeout=args.timeout,
                open_timeout=args.open_timeout, log_path=args.log, trigger=None)
        summarize(args.log, "real-s3-v4a", args.safe_percent)
        return 0

    if not args.port and not args.dry_run:
        raise SystemExit("--port is required for a physical run")
    trigger = None if args.no_trigger else b"fm6audiobench\r\n"
    if args.dry_run:
        print(f"would capture {args.port or 'PORT'} -> {args.log}, trigger={trigger!r}, parse as real-ac7911b8-v4a")
        return 0
    capture(port=args.port, baud=args.baud, timeout=args.timeout,
            open_timeout=args.open_timeout, log_path=args.log, trigger=trigger)
    summarize(args.log, "real-ac7911b8-v4a", args.safe_percent)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
