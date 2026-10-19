#!/usr/bin/env python3
"""Read-only candidate inventory, never a constant-replacement patcher.

Use the symbol-bearing analysis extraction of the ARM donor, not the rebuilt
runtime image whose local symbol table may be absent. Output addresses refer
only to that input image; they are not file offsets or runtime patch addresses.
"""
import argparse
import json
import re
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("image")
args = p.parse_args()
assembly = subprocess.check_output(
    ["xcrun", "llvm-objdump", "-d", args.image], text=True)
heading = re.compile(r"^([0-9a-f]+) <(.+)>:$")
candidate = re.compile(r"(?:lsl|lsr|ubfiz|ubfx|sbfiz|sbfx).*#14\b|#0x(?:3fff|4000)\b")
rows = []
current = None
for line in assembly.splitlines():
    match = heading.match(line)
    if match:
        current = {"address": "0x" + match[1], "function": match[2],
                   "instructions": []}
    elif current is not None and candidate.search(line):
        if not current["instructions"]:
            rows.append(current)
        current["instructions"].append(line.strip())
if not rows or all(r["function"] == "__text" for r in rows):
    raise SystemExit("Need a symbol-bearing ARM analysis image; no useful inventory")
print(json.dumps({"image": args.image,
    "warning": "Candidates only: includes unrelated bitfields and host geometry; not patch sites",
    "function_count": len(rows),
    "instruction_count": sum(len(r["instructions"]) for r in rows),
    "functions": rows}, indent=2))
