#!/usr/bin/env python3
"""Drops what Rosetta can't be compared on. 16-bit shld and shrd lines whose masked count is above 16: undefined in the SDM. And test_fenv's first two
status lines, from data16 fnstenv and fnsave, which Rosetta writes in the 32-bit layout; hybrid.c checks those against the SDM."""
import re
import sys

status = 0
for line in sys.stdin:
    m = re.match(r"sh[lr]dw\s.* C=([0-9a-f]+)", line)
    if m and int(m.group(1), 16) & 31 > 16:
        continue
    if line.startswith("fpuc="):
        status += 1
        if status <= 2:
            continue
    sys.stdout.write(line)
