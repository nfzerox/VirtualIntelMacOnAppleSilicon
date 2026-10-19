#!/usr/bin/env python3
"""Give the private re-export facade the ABI version its clients require."""
import struct
import sys
from pvg_macho import Image, require, save

image = Image(open(sys.argv[1], 'rb').read())
data = bytearray(image.data)
p = 32
sites = []
for _ in range(struct.unpack_from('<I', data, 16)[0]):
    cmd, size = struct.unpack_from('<II', data, p)
    if cmd == 13:
        sites.append(p)
    p += size
require(len(sites) == 1, 'Expected one dylib ID')
p = sites[0]
require(struct.unpack_from('<II', data, p + 16) in ((0, 0), (65536, 65536)),
        'Unexpected facade ABI version')
struct.pack_into('<II', data, p + 16, 65536, 65536)
save(sys.argv[2], image.preserving_slices(data))
