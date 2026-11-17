#!/usr/bin/env python3
"""A flat linker for ELF32 relocatable x86 objects. macOS's ld no longer links i386 and no other linker is here, and these tests need only this.
Usage: link32.py <base> <output> <object>... Every allocated section goes in the order given, the first object's .text first, all writable and
executable. __start_X and __stop_X exist for each section X with a C name. Prints nothing; an undefined symbol is an error."""
import struct
import sys

base, output, objects = int(sys.argv[1], 0), sys.argv[2], sys.argv[3:]
SHT_SYMTAB, SHT_NOBITS, SHT_REL, SHF_ALLOC = 2, 8, 9, 2
R_32, R_PC32, R_PLT32, R_16, R_PC16 = 1, 2, 4, 20, 21


class Obj:
    def __init__(self, path):
        self.path, self.d = path, open(path, "rb").read()
        d = self.d
        assert d[:6] == b"\x7fELF\x01\x01", path
        shoff, = struct.unpack_from("<I", d, 32)
        shentsize, shnum, shstrndx = struct.unpack_from("<HHH", d, 46)
        self.sh = [struct.unpack_from("<10I", d, shoff + i * shentsize) for i in range(shnum)]
        names = self.sh[shstrndx][4]
        self.names = [d[names + s[0]:d.index(b"\0", names + s[0])].decode() for s in self.sh]
        self.addr = {}


objs = [Obj(p) for p in objects]
at = base
image = bytearray()
ranges = {}


def place(o, i):
    global at, image
    s = o.sh[i]
    align = max(s[8], 1)
    at = (at + align - 1) & ~(align - 1)
    image.extend(b"\0" * (at - base - len(image)))
    o.addr[i] = at
    image.extend(b"\0" * s[5] if s[1] == SHT_NOBITS else o.d[s[4]:s[4] + s[5]])
    at += s[5]
    lo, hi = ranges.get(o.names[i], (at - s[5], at))
    ranges[o.names[i]] = (min(lo, at - s[5]), at)


# Text first, so the first object's entry stays at the base. Sections of one name stay together, which __start and __stop need.
order = []
for o in objs:
    for i, s in enumerate(o.sh):
        if s[2] & SHF_ALLOC and o.names[i] not in order:
            order.append(o.names[i])
order.sort(key=lambda n: (not n.startswith(".text")))
for name in order:
    for o in objs:
        for i, s in enumerate(o.sh):
            if s[2] & SHF_ALLOC and o.names[i] == name:
                place(o, i)

globals_ = {}
for name, (lo, hi) in ranges.items():
    if name.replace("_", "a").isalnum():
        globals_["__start_" + name] = lo
        globals_["__stop_" + name] = hi


def symbols(o):
    i = next(i for i, s in enumerate(o.sh) if s[1] == SHT_SYMTAB)
    s = o.sh[i]
    strs = o.sh[s[6]][4]
    out = []
    for n in range(s[5] // 16):
        name, value, size, info, other, shndx = struct.unpack_from("<IIIBBH", o.d, s[4] + n * 16)
        out.append((o.d[strs + name:o.d.index(b"\0", strs + name)].decode(), value, info, shndx))
    return out


for o in objs:
    o.syms = symbols(o)
    for name, value, info, shndx in o.syms:
        if info >> 4 and shndx and shndx < 0xff00 and shndx in o.addr:
            globals_[name] = o.addr[shndx] + value
        elif info >> 4 and shndx == 0xfff1:
            globals_[name] = value


for o in objs:
    for i, s in enumerate(o.sh):
        target = s[7]
        if s[1] != SHT_REL or target not in o.addr:
            continue
        for n in range(s[5] // 8):
            off, info = struct.unpack_from("<II", o.d, s[4] + n * 8)
            kind, sym = info & 0xff, info >> 8
            name, value, inf, shndx = o.syms[sym]
            if shndx == 0:
                if name not in globals_:
                    sys.exit(f"{o.path}: undefined {name}")
                sv = globals_[name]
            elif shndx == 0xfff1:
                sv = value
            else:
                sv = o.addr[shndx] + value
            place_at = o.addr[target] + off
            p = place_at - base
            if kind == R_32:
                struct.pack_into("<I", image, p, (struct.unpack_from("<I", image, p)[0] + sv) & 0xffffffff)
            elif kind in (R_PC32, R_PLT32):
                struct.pack_into("<I", image, p, (struct.unpack_from("<I", image, p)[0] + sv - place_at) & 0xffffffff)
            elif kind == R_16:
                struct.pack_into("<H", image, p, (struct.unpack_from("<H", image, p)[0] + sv) & 0xffff)
            elif kind == R_PC16:
                struct.pack_into("<H", image, p, (struct.unpack_from("<H", image, p)[0] + sv - place_at) & 0xffff)
            else:
                sys.exit(f"{o.path}: relocation {kind}")
open(output, "wb").write(image)
