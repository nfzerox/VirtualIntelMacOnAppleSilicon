#!/usr/bin/env python3
"""Finite, development-only frame capture across real app-owned VM reboots.

Use an already running app's --guest-diagnostics directory. Captures reuse its
normal presentation texture, never another QMP/QGA client or a native window
ID that may disappear at reboot. This is not a frame-rate benchmark: PNG
readback deliberately adds work. No guest commands or lifecycle actions run.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("diagnostics", type=Path)
    parser.add_argument("output", type=Path, help="A new directory outside the VM bundle")
    parser.add_argument("--seconds", type=float, default=120)
    parser.add_argument("--interval", type=float, default=1)
    args = parser.parse_args()
    if args.seconds <= 0 or args.interval <= 0:
        parser.error("Choose positive capture duration and interval")
    args.output.mkdir(mode=0o700)
    adapter = Path(__file__).with_name("app-guest-command.py")
    start = time.monotonic()
    count = 0
    with (args.output / "Frames.jsonl").open("x") as log:
        while time.monotonic() - start < args.seconds:
            # A guest command during reboot may still own the diagnostic slot.
            # Observe it without removing, replacing or replaying that request.
            if any((args.diagnostics / name).exists() for name in ("Request.json", "Running.json")):
                time.sleep(args.interval)
                continue
            requested = time.time_ns()
            result = subprocess.run((sys.executable, adapter, args.diagnostics, "--capture"),
                                    capture_output=True, text=True)
            try:
                record = json.loads(result.stdout)
            except ValueError:
                record = {"Error": result.stderr.strip() or "No capture result"}
            record["ElapsedSeconds"] = round(time.monotonic() - start, 3)
            frame = args.diagnostics / "Frame.png"
            if record.get("Captured") is True and frame.stat().st_mtime_ns >= requested:
                count += 1
                name = f"{count:04d}-{record['Session']}.png"
                shutil.copyfile(frame, args.output / name)
                record["File"] = name
            log.write(json.dumps(record) + "\n")
            log.flush()
            print(json.dumps(record), flush=True)
            time.sleep(args.interval)
    print(f"Saved {count} current-session frames; no guest state changed.")


if __name__ == "__main__":
    main()
