#!/usr/bin/env python3
"""lc-bench-style table: engine, Rosetta and native, best of N runs per test. One process at a time, each well under 30 seconds.
Usage: bench.py [runs] [budget in hundredths of a second]"""
import os
import subprocess
import sys

here = os.path.dirname(os.path.abspath(__file__))
images = os.path.join(here, "..", ".build", "images")
engine = os.path.join(here, "..", ".build", "release", "engine-run")
runs = int(sys.argv[1]) if len(sys.argv) > 1 else 3
budget = sys.argv[2] if len(sys.argv) > 2 else "20"
image = os.path.join(images, "t_lcbench")
columns = {
    "engine": ([engine, image, budget], {}),
    "engine-fast": ([engine, image, budget], {"ENGINE_FAST_FLAGS": "1"}),
    "rosetta-flat": (["arch", "-x86_64", os.path.join(images, "flatrun"), image, budget], {}),
    "native": ([os.path.join(images, "lc-bench-arm64"), "-t", str(int(budget) / 100)], {}),
    "rosetta-full": (["arch", "-x86_64", os.path.join(images, "lc-bench-x86"), "-t", str(int(budget) / 100)], {}),
}
keep = ["alu", "branchy", "chase_16k", "chase_1m", "chase_64m", "memcpy", "sse_float", "scalar_double", "cas_uncontended", "largecode_cold", "largecode_warm"]
best = {c: {} for c in columns}
units = {}
for r in range(runs):
    for name, (cmd, env) in columns.items():
        if name in ("native", "rosetta-full") and r > 0 and runs > 1 and os.environ.get("BENCH_FAST"):
            continue
        out = subprocess.run(cmd, env={**os.environ, **env}, capture_output=True, text=True, timeout=120)
        for line in out.stdout.splitlines():
            p = line.split()
            if len(p) == 3 and p[0] in keep:
                v = float(p[1])
                units[p[0]] = p[2]
                higher = p[2] == "MB/s"
                b = best[name].get(p[0])
                if b is None or (v > b if higher else v < b):
                    best[name][p[0]] = v
names = list(columns)
print("| test | unit | " + " | ".join(names) + " | engine/rosetta-flat | engine/native |")
print("|---|---|" + "---|" * (len(names) + 2))
for t in keep:
    row = [t, units.get(t, "")]
    for n in names:
        row.append(f"{best[n].get(t, float('nan')):.3f}" if t in best[n] else "")
    e, ro, na = best["engine"].get(t), best["rosetta-flat"].get(t), best["native"].get(t)
    higher = units.get(t) == "MB/s"
    ratio = (lambda a, b: (b / a if higher else a / b) if a and b else float("nan"))
    row.append(f"{ratio(e, ro):.2f}")
    row.append(f"{ratio(e, na):.2f}")
    print("| " + " | ".join(row) + " |")
