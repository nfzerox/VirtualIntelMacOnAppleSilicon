#!/usr/bin/env python3
"""Turns the compat kernel, a 64-bit Mach-O, into what QEMU's TCG boots: a 64 KB firmware image from boot.bin and a RAM image for -device loader at 1 MB.
The RAM image holds the same tables and frames as KernelGuest.buildBootTables, with low RAM identity-mapped as well for the boot code.
Usage: mkqemu.py <kernel> <boot.bin> <rom out> <ram out> <RAM bytes>"""
import struct
import sys

kernel = open(sys.argv[1], "rb").read()
boot = open(sys.argv[2], "rb").read()
ram_bytes = int(sys.argv[5], 0)

assert struct.unpack_from("<I", kernel, 0)[0] == 0xFEEDFACF
ncmds = struct.unpack_from("<I", kernel, 16)[0]
at, segments, entry = 32, [], None
for _ in range(ncmds):
    cmd, size = struct.unpack_from("<II", kernel, at)
    if cmd == 0x19:
        name = kernel[at + 8:at + 24].rstrip(b"\0").decode()
        vmaddr, vmsize, fileoff, filesize = struct.unpack_from("<QQQQ", kernel, at + 24)
        segments.append((name, vmaddr, vmsize, fileoff, filesize))
    elif cmd == 5:  # LC_UNIXTHREAD: flavor, count, then rax.. with rip the 17th
        entry = struct.unpack_from("<Q", kernel, at + 16 + 16 * 8)[0]
    at += size
assert entry

BASE = 1 << 20
ram = bytearray(64 << 20)
next_frame = BASE


def frame():
    global next_frame
    f = next_frame
    next_frame += 4096
    return f


def put(table, index, value):
    struct.pack_into("<Q", ram, table - BASE + index * 8, value)


def get(table, index):
    return struct.unpack_from("<Q", ram, table - BASE + index * 8)[0]


RW = 3
root = frame()
physmap = frame()
put(root, (0xFFFF880000000000 >> 39) & 0x1FF, physmap | RW)
for gb in range(0, ram_bytes, 1 << 30):
    put(physmap, gb >> 30, gb | RW | 0x80)
low = frame()
put(root, 0, low | RW)  # for the boot code only: new address spaces take the kernel half alone
put(low, 0, 0 | RW | 0x80)


def walk(va):
    table = root
    for level in (3, 2, 1):
        index = (va >> (12 + 9 * level)) & 0x1FF
        e = get(table, index)
        if not e & 1:
            e = frame() | RW
            put(table, index, e)
        table = e & 0x000FFFFFFFFFF000
    return table


image_frame = 2 << 20
for name, vmaddr, vmsize, fileoff, filesize in segments:
    if vmsize == 0 or name == "__LINKEDIT":
        continue
    for off in range(0, vmsize, 4096):
        va = vmaddr + off
        put(walk(va), (va >> 12) & 0x1FF, image_frame | RW)
        if off < filesize:
            n = min(4096, filesize - off)
            ram[image_frame - BASE:image_frame - BASE + n] = kernel[fileoff + off:fileoff + off + n]
        image_frame += 4096
assert image_frame < 32 << 20 and next_frame < 2 << 20

rom = bytearray(b"\xff" * 0x10000)
rom[0:len(boot)] = boot
struct.pack_into("<QQ", rom, 0xFFE0, entry, ram_bytes)
rom[0xFFF0:0xFFF5] = b"\xEA\x00\x00\x00\xF0"  # ljmp $0xF000, $0
open(sys.argv[3], "wb").write(rom)
open(sys.argv[4], "wb").write(ram[:image_frame - BASE])
