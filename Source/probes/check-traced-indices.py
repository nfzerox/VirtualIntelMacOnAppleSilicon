#!/usr/bin/env python3
"""Inspect actual index values in a compact PVG command trace commit."""

import os
import re
import struct
import sys


def field(line, name):
    match = re.search(r"(?:^|\s)" + name + r"=([^\s]+)", line)
    return match.group(1) if match else None


def main(directory, commit):
    active = False
    draws = []
    with open(os.path.join(directory, "manifest.txt")) as manifest:
        for line in manifest:
            if line.startswith("commit sequence="):
                if active:
                    break
                active = int(field(line, "sequence")) == commit
                continue
            if not active or not line.startswith("record sequence="):
                continue
            if int(field(line, "opcode"), 16) not in (6, 7):
                continue
            with open(field(line, "path"), "rb") as records:
                records.seek(int(field(line, "offset")))
                data = records.read(int(field(line, "size")))
            opcode = struct.unpack_from("<I", data)[0]
            if opcode == 6:
                primitive, index_type, ref, count, offset = struct.unpack_from(
                    "<HHIQQ", data, 8)
            else:
                primitive, index_type, ref, count, offset = struct.unpack_from(
                    "<HHIHH", data, 8)
            snapshot = os.path.join(directory, "commit-%03d-buffer-%x.bin" %
                                    (commit, ref))
            if not os.path.exists(snapshot):
                continue
            stride = 2 if index_type == 0 else 4
            with open(snapshot, "rb") as buffer:
                buffer.seek(offset)
                payload = buffer.read(count * stride)
            if len(payload) != count * stride:
                print("truncated", field(line, "sequence"), ref, count, offset)
                continue
            values = struct.unpack("<%d%s" %
                                   (count, "H" if stride == 2 else "I"),
                                   payload)
            draws.append((int(field(line, "sequence")), ref, count, offset,
                          min(values), max(values), len(set(values))))
    print("commit=%d indexed_draws_with_snapshots=%d" % (commit, len(draws)))
    for draw in sorted(draws, key=lambda draw: draw[5], reverse=True)[:20]:
        print("draw=%d buffer=%#x count=%d offset=%d min=%d max=%d unique=%d"
              % draw)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("usage: check-traced-indices.py trace-directory commit")
    main(sys.argv[1], int(sys.argv[2]))
