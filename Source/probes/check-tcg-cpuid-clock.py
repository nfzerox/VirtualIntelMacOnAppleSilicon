#!/usr/bin/env python3
"""Read real TCG CPUID leaves from a disposable BIOS sector on Darwin ARM."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qemu", type=Path)
    parser.add_argument("--bios-directory", type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="vim-cpuid-clock-") as folder:
        sector = Path(folder) / "probe.img"
        subprocess.run(("nasm", "-f", "bin", Path(__file__).with_name("tcg-cpuid-clock.asm"),
                        "-o", sector), check=True)
        cases = (("Haswell-noTSX", True, True, False),
                 ("Haswell-noTSX", False, True, False),
                 ("Haswell-noTSX", True, False, False),
                 ("Haswell-noTSX", True, True, True),
                 ("Penryn", True, True, False), ("Penryn", False, True, False))
        for model, enabled, rate, icount in cases:
            cpu = model + (",stepping=4" if model == "Haswell-noTSX" else "")
            cpu += f",vmware-cpuid-freq={'on' if enabled else 'off'}"
            if rate:
                cpu += ",tsc-frequency=1200000000"
            command = [str(args.qemu.resolve()), "-L", str(args.bios_directory.resolve()),
                       "-machine", "pc", "-accel", "tcg", "-cpu", cpu, "-smp", "1", "-m", "16",
                       "-display", "none", "-serial", "none", "-monitor", "none", "-debugcon", "stdio",
                       "-device", "isa-debug-exit,iobase=0xf4,iosize=4",
                       "-drive", f"file={sector},format=raw,if=ide,snapshot=on"]
            if icount:
                command += ["-icount", "shift=3,align=off,sleep=off"]
            result = subprocess.run(command, capture_output=True, text=True, timeout=30)
            lines = result.stdout.splitlines()
            if result.returncode != 33 or len(lines) != 6 or not all(
                    re.fullmatch("[0-9A-F]{32}", line) for line in lines):
                raise RuntimeError((result.returncode, result.stdout, result.stderr))
            rows = [[int(line[i:i+8], 16) for i in range(0, 32, 8)] for line in lines]
            clock = enabled and rate and not icount
            assert rows[0][0] == (0x40000010 if clock else 0x40000001), rows
            assert b"".join(n.to_bytes(4, "little") for n in rows[0][1:]) == b"TCGTCGTCGTCG"
            if clock:
                assert rows[1] == [0, 0, 0, 0], rows
                assert rows[2] == [1200000, 1000000, 0, 0], rows
            features = (1 << 12) | (1 << 20) | (1 << 22) | (1 << 23) | (1 << 28)
            leaf7 = (1 << 3) | (1 << 5) | (1 << 8)
            haswell = (rows[3][2] & features == features and rows[4][1] & leaf7 == leaf7
                       and bool(rows[5][2] & (1 << 5)))
            assert haswell == (model == "Haswell-noTSX"), rows
            if haswell:
                assert rows[3][0] & 15 == 4, rows
            print(f"PASS {model}: clock={clock}, explicit-rate={rate}, icount={icount}, x86_64h={haswell}", flush=True)


if __name__ == "__main__":
    main()
