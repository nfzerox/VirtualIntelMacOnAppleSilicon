#!/usr/bin/env python3
"""The part of the compat kernel's output that is compared: no timings, and none of the forms named in the file given, whose reasons it has."""
import re
import sys

known = {l.rstrip("\n") for l in open(sys.argv[1]) if not l.startswith("#")}
for line in sys.stdin.buffer.read().decode("latin-1").splitlines():
    m = re.match(r" *\d+ [0-9a-f]{16} (.*)", line)
    if not line.startswith("measure") and not (m and m.group(1) in known):
        print(line)
