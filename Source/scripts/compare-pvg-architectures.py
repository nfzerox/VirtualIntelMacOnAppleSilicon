#!/usr/bin/env python3
"""Pair symbol-bearing ARM/Intel donor functions for review, never auto-patch.

Writes complete paired assembly plus decompiler manifests. Candidate arithmetic
is deliberately broad; compiler differences and host-only geometry are expected.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("arm")
p.add_argument("intel")
p.add_argument("output", type=Path)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
heading = re.compile(r"^([0-9a-f]+) <(.+)>:$")
def functions(path):
    text = subprocess.check_output(["xcrun", "llvm-objdump", "-d", path], text=True)
    result = {}
    current = None
    for line in text.splitlines():
        m = heading.match(line)
        if m:
            current = {"address": m[1], "name": m[2], "assembly": []}
            result[m[2]] = current
        elif current is not None and line.strip():
            current["assembly"].append(line)
    return result
arm, intel = functions(a.arm), functions(a.intel)
# Include arithmetic-derived geometry as well as direct literals. These are
# review hints, not evidence that every match is a page-size operation.
candidate = re.compile(r"#(?:0x(?:3fff|4000|fff|1000)\b|(?:12|14|18|20)\b)|"
                       r"#0x[f3]+[cfe]000\b|#0x4, lsl #12")
rows = []
for name, af in arm.items():
    if "PG" not in name or name.startswith("__ZNSt"):
        continue
    xf = intel.get(name)
    hints = [l for l in af["assembly"] if candidate.search(l)]
    rows.append({"name": name, "arm": af["address"],
                 "intel": xf["address"] if xf else None, "hints": hints})
rows.sort(key=lambda r: (not bool(r["hints"]), r["name"]))
for arch, table in (("arm", arm), ("intel", intel)):
    with (a.output / (arch + ".tsv")).open("w") as manifest, \
         (a.output / (arch + ".asm")).open("w") as assembly:
        for row in rows:
            if not row[arch]:
                continue
            manifest.write(row[arch] + "\t" + row["name"] + "\n")
            f = table[row["name"]]
            assembly.write("\n=== " + row["name"] + " ===\n" +
                           "\n".join(f["assembly"]) + "\n")
report = {"arm_image": a.arm, "intel_image": a.intel,
          "arm_functions": len(rows),
          "paired_functions": sum(r["intel"] is not None for r in rows),
          "candidate_functions": sum(bool(r["hints"]) for r in rows),
          "candidate_instructions": sum(len(r["hints"]) for r in rows),
          "warning": "Assembly review inventory, not a semantic diff or exhaustive patch list",
          "functions": rows}
(a.output / "inventory.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps({k: v for k, v in report.items() if k != "functions"}, indent=2))
