#!/usr/bin/env python3
"""Capture V4A on a physical serial port; preserve partial bytes on every failure."""
from __future__ import annotations
import argparse
import hashlib
import json
import math
import re
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from run_v3_hardware import detect_esptool_write_command, run_checked, wait_open
from parse_v4a_audio import PLATFORMS, evaluate

DONE = re.compile(rb"(?:^|\n)FM6-V4A DONE\r?\n")


def capture(*, port: str, baud: int, timeout: float, open_timeout: float,
            log_path: Path, trigger: bytes | None, overwrite: bool = False) -> str:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    data = bytearray()
    ser = None
    with log_path.open("wb" if overwrite else "xb") as out:
        try:
            ser = wait_open(port, baud, open_timeout)
            print(f"opened {port}; S3 --no-flash users: reset the board now", flush=True)
            # Do not discard queued S3 boot/header bytes after USB reconnect.
            if trigger is not None:
                ser.reset_input_buffer()
                ser.write(trigger)
                ser.flush()
            start = time.monotonic()
            while time.monotonic() - start < timeout:
                chunk = ser.readline()
                if not chunk:
                    continue
                out.write(chunk)
                out.flush()
                data.extend(chunk)
                print(chunk.decode("utf-8", errors="replace"), end="", flush=True)
                if DONE.search(data):
                    break
        finally:
            if ser is not None:
                ser.close()
            print(f"raw capture retained: {log_path}", flush=True)
    return bytes(data).decode("utf-8", errors="replace")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="target", required=True)
    for target in ("s3", "fm1"):
        p = sub.add_parser(target)
        p.add_argument("--port")
        p.add_argument("--baud", type=int, default=115200)
        p.add_argument("--timeout", type=float, default=45)
        p.add_argument("--open-timeout", type=float, default=10)
        p.add_argument("--safe-percent", type=float, default=85)
        p.add_argument("--log", type=Path, default=Path(f"build/real-{target}-v4a.log"))
        p.add_argument("--overwrite-log", action="store_true")
        p.add_argument("--dry-run", action="store_true")
        if target == "s3":
            p.add_argument("--image", type=Path, default=Path("flash_image.bin"))
            p.add_argument("--flash-baud", type=int, default=921600)
            p.add_argument("--no-flash", action="store_true", help="listen only; reset AFTER the port opens")
        else:
            p.add_argument("--no-trigger", action="store_true")
    args = ap.parse_args()
    for key in ("timeout", "open_timeout"):
        value = getattr(args, key)
        if not math.isfinite(value) or value <= 0:
            ap.error(f"{key} must be positive and finite")
    if not math.isfinite(args.safe_percent) or not 0 < args.safe_percent < 100:
        ap.error("safe-percent must be finite and strictly between 0 and 100")
    if not args.port and not args.dry_run:
        ap.error("--port is required")
    if args.log.exists() and not args.overwrite_log and not args.dry_run:
        ap.error("log already exists: choose a new --log or explicitly --overwrite-log")
    image_sha = None
    trigger = None
    if args.target == "s3" and not args.no_flash:
        if not args.image.is_file() and not args.dry_run:
            ap.error(f"image not found: {args.image}")
        if not args.dry_run:
            image_sha = hashlib.sha256(args.image.read_bytes()).hexdigest()
        cmd = detect_esptool_write_command(args.dry_run)
        run_checked([sys.executable,"-m","esptool","--chip","esp32s3","--port",args.port or "PORT",
                     "--baud",str(args.flash_baud),cmd,"0x0",str(args.image)], dry_run=args.dry_run)
    elif args.target == "fm1" and not args.no_trigger:
        trigger = b"fm6audiobench\r\n"
    if args.dry_run:
        print(f"would capture {args.port or 'PORT'} -> {args.log}, trigger={trigger!r}, parse as real-{args.target}-v4a")
        return 0
    metadata = dict(source="physical", provenance="serial-capture-not-hardware-attestation",
                    started_utc=datetime.now(timezone.utc).isoformat(), target=args.target,
                    port=args.port, image_sha256=image_sha, complete=False)
    meta_path = args.log.with_suffix(".capture.json")
    try:
        text = capture(port=args.port, baud=args.baud, timeout=args.timeout, open_timeout=args.open_timeout,
                       log_path=args.log, trigger=trigger, overwrite=args.overwrite_log)
        result = evaluate(text, args.safe_percent, "physical")
        wanted = PLATFORMS[0 if args.target == "s3" else 1]
        if result["platform"] != wanted:
            raise ValueError(f"wrong platform: expected {wanted}, got {result['platform']}")
        result["capture"] = metadata.copy()
        result["capture"]["complete"] = True
        args.log.with_suffix(".summary.json").write_text(json.dumps(result,indent=2)+"\n")
        metadata["complete"] = True
        run_checked([sys.executable,str(Path(__file__).with_name("parse_v4a_audio.py")),
                     "--label",f"real-{args.target}-v4a","--source","physical",
                     "--safe-percent",str(args.safe_percent),str(args.log)])
    except Exception as exc:
        metadata["error"] = str(exc)
        print(f"capture/validation failed: {exc}", file=sys.stderr)
        return 2
    finally:
        args.log.parent.mkdir(parents=True,exist_ok=True)
        if args.log.is_file():
            metadata["log_sha256"] = hashlib.sha256(args.log.read_bytes()).hexdigest()
        meta_path.write_text(json.dumps(metadata,indent=2)+"\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
