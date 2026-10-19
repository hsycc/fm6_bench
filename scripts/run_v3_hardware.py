#!/usr/bin/env python3
"""Flash/trigger, capture, and summarize FM6 V3 physical-board benchmarks.

Examples:
  python3 scripts/run_v3_hardware.py s3 --port /dev/cu.usbmodem101 --image flash_image.bin
  python3 scripts/run_v3_hardware.py fm1 --port /dev/cu.usbmodem1101

The script intentionally keeps QEMU out of the physical-data path. Successful
runs must contain an FM6-V3 header, all configured rows, and FM6-V3 DONE before
the existing parser is invoked.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
import time
from pathlib import Path

DONE = "FM6-V3 DONE"
EXPECTED_VOICES = (1, 8, 12, 16, 20, 24)


def require_serial():
    try:
        import serial  # type: ignore
    except ImportError as exc:
        raise SystemExit(
            "pyserial is required for physical capture: python3 -m pip install pyserial"
        ) from exc
    return serial


def run_checked(cmd: list[str], dry_run: bool = False) -> None:
    print("+", " ".join(cmd), flush=True)
    if not dry_run:
        subprocess.run(cmd, check=True)


def wait_open(port: str, baud: int, open_timeout: float):
    serial = require_serial()
    deadline = time.monotonic() + open_timeout
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            return serial.Serial(port, baudrate=baud, timeout=0.1)
        except Exception as exc:  # board may be reconnecting after flash/reset
            last_error = exc
            time.sleep(0.25)
    raise SystemExit(f"could not open serial port {port!r}: {last_error}")


def validate_log(text: str) -> None:
    if "FM6-V3 platform=" not in text:
        raise SystemExit("capture ended without an FM6-V3 header")
    if DONE not in text:
        raise SystemExit("capture ended before FM6-V3 DONE")
    missing = [v for v in EXPECTED_VOICES if f"FM6-V3 voices={v} " not in text]
    if missing:
        raise SystemExit(f"capture is missing voice rows: {missing}")


def capture(
    *,
    port: str,
    baud: int,
    timeout: float,
    open_timeout: float,
    log_path: Path,
    trigger: bytes | None,
) -> None:
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
    parser = Path(__file__).with_name("parse_v3_runtime.py")
    run_checked([
        sys.executable,
        str(parser),
        "--label",
        label,
        "--safe-percent",
        str(safe_percent),
        str(log_path),
    ])


def add_common_capture_args(ap: argparse.ArgumentParser, default_log: str) -> None:
    ap.add_argument("--port", required=False, help="serial device, e.g. /dev/cu.usbmodem101 or COM6")
    ap.add_argument("--baud", type=int, default=115200, help="serial capture baud (default 115200)")
    ap.add_argument("--timeout", type=float, default=30.0, help="seconds to wait for FM6-V3 DONE")
    ap.add_argument("--open-timeout", type=float, default=10.0, help="seconds to wait for serial reconnect")
    ap.add_argument("--log", type=Path, default=Path(default_log), help="raw capture output path")
    ap.add_argument("--safe-percent", type=float, default=85.0, help="p99 utilization safety gate")
    ap.add_argument("--dry-run", action="store_true", help="validate arguments and print actions without hardware")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="target", required=True)

    s3 = sub.add_parser("s3", help="flash/capture a physical ESP32-S3")
    add_common_capture_args(s3, "build/real-s3.log")
    s3.add_argument("--image", type=Path, default=Path("flash_image.bin"), help="merged image written at offset 0")
    s3.add_argument("--flash-baud", type=int, default=921600, help="esptool flashing baud")
    s3.add_argument("--no-flash", action="store_true", help="skip flashing and only capture serial output")

    fm1 = sub.add_parser("fm1", help="trigger/capture the Felucca fm6bench console command")
    add_common_capture_args(fm1, "build/real-ac7911b8.log")
    fm1.add_argument("--no-trigger", action="store_true", help="listen only; do not send fm6bench")

    args = ap.parse_args()

    if args.target == "s3":
        if not args.no_flash and not args.image.is_file() and not args.dry_run:
            raise SystemExit(f"merged image not found: {args.image}")
        if not args.port and not args.dry_run:
            raise SystemExit("--port is required for a physical run")
        if not args.no_flash:
            run_checked([
                sys.executable,
                "-m",
                "esptool",
                "--chip",
                "esp32s3",
                "--port",
                args.port or "PORT",
                "--baud",
                str(args.flash_baud),
                "write_flash",
                "0x0",
                str(args.image),
            ], dry_run=args.dry_run)
        if args.dry_run:
            print(f"would capture {args.port or 'PORT'} -> {args.log} and parse as real-s3")
            return 0
        capture(
            port=args.port,
            baud=args.baud,
            timeout=args.timeout,
            open_timeout=args.open_timeout,
            log_path=args.log,
            trigger=None,
        )
        summarize(args.log, "real-s3", args.safe_percent)
        return 0

    if not args.port and not args.dry_run:
        raise SystemExit("--port is required for a physical run")
    trigger = None if args.no_trigger else b"fm6bench\r\n"
    if args.dry_run:
        print(f"would capture {args.port or 'PORT'} -> {args.log}, trigger={trigger!r}, parse as real-ac7911b8")
        return 0
    capture(
        port=args.port,
        baud=args.baud,
        timeout=args.timeout,
        open_timeout=args.open_timeout,
        log_path=args.log,
        trigger=trigger,
    )
    summarize(args.log, "real-ac7911b8", args.safe_percent)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
