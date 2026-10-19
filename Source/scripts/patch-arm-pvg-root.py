#!/usr/bin/env python3
"""Guarded ARM26 guest-memory geometry experiment; NOT complete Intel support.

Adapt root/FIFO/display, range tracking, task mappings and object-list rounding.
Requires the separate Intel page-table walker; resource ABI and Metal import
compatibility still need runtime qualification. Never apply to the host cache.
"""
import argparse
import hashlib
from pathlib import Path
import struct

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("input", type=Path)
p.add_argument("output", type=Path)
a = p.parse_args()
data = bytearray(a.input.read_bytes())
expected = {
    "b0086ce4830d1a38285f2fa430fbbc042fe0ef65fb36154a3f3f44eb7aa4ccfb",
    # Same ARM26 text layout, with auth-stub export resolvers reconstructed.
    "948c9943fae662971c86c74687cb9c66670ab9678bc54edd1f88635213a7ead3",
}
if hashlib.sha256(data).hexdigest() not in expected:
    raise SystemExit("Unexpected extracted ARM26 donor; refusing all patches")
segments = []
pos = 32
for _ in range(struct.unpack_from("<I", data, 16)[0]):
    cmd, size = struct.unpack_from("<II", data, pos)
    if cmd == 0x19:
        va, vs, offset, fs = struct.unpack_from("<QQQQ", data, pos + 24)
        segments.append((va, offset, fs))
    pos += size
# RVAs preserved by this exact rebuilt donor. Each instruction is checked.
patches = [
    # Reviewed PGLocalTask / PGObjectListManager guest-address sites only.
    # Host allocation alignment, Metal texture bitfields and unrelated masks
    # must not be changed by a blanket 16K->4K substitution.
    (0x2fdb8, 0xf240369f, 0xf2402e9f, "task map offset alignment 4K"),
    (0x2fdc0, 0xf240367f, 0xf2402e7f, "task map length alignment 4K"),
    (0x3009c, 0xd3727c56, 0xd3747c56, "task map physical PFN shift 12"),
    (0x300b0, 0x91401129, 0x91400529, "task map adjacent physical page 4K"),
    (0x300d4, 0x91401129, 0x91400529, "task map coalesced length increment 4K"),
    (0x30138, 0x52880009, 0x52820009, "task map initial range 4K"),
    (0x30550, 0xf240345f, 0xf2402c5f, "virtual map offset alignment 4K"),
    (0x30558, 0xf240367f, 0xf2402e7f, "virtual map length alignment 4K"),
    (0x308c0, 0xd3727c55, 0xd3747c55, "virtual map physical PFN shift 12"),
    (0x308ec, 0x9140112a, 0x9140052a, "virtual map adjacent page 4K"),
    (0x308fc, 0x91401129, 0x91400529, "virtual map coalesced increment 4K"),
    (0x30930, 0x52880009, 0x52820009, "virtual map initial range 4K"),
    (0x30b0c, 0x9272c442, 0x9274cc42, "memory map offset round down 4K"),
    (0x30b10, 0x5287ffe8, 0x5281ffe8, "memory map rounding bias 4K"),
    (0x30b1c, 0x9272c508, 0x9274cd08, "memory map length round down 4K"),
    (0x30b44, 0x92403673, 0x92402e73, "memory map subpage offset 4K"),
    (0x30d44, 0xd3727c42, 0xd3747c42, "mapped address PFN shift 12"),
    (0x30d6c, 0xd3727c41, 0xd3747c41, "validate mapped range PFN shift 12"),
    (0x30d7c, 0xab093869, 0xab093069, "validate mapped range bound shift 12"),
    (0x30fa8, 0xd3727c42, 0xd3747c42, "memory map buffer PFN shift 12"),
    (0x32a4c, 0x9240344a, 0x92402c4a, "copy subpage offset 4K"),
    (0x32a50, 0x9272c442, 0x9274cc42, "copy start round down 4K"),
    (0x32a5c, 0x5287ffe9, 0x5281ffe9, "copy length rounding bias 4K"),
    (0x32a6c, 0x9272c508, 0x9274cd08, "copy length rounding mask 4K"),
    (0x32ba0, 0xd3727c42, 0xd3747c42, "copy physical PFN shift 12"),
    (0x32ba4, 0x91401048, 0x91400448, "copy page end 4K"),
    (0x32ea8, 0x9272c6c2, 0x9274cec2, "cursor start round down 4K"),
    (0x32eac, 0x5287ffea, 0x5281ffea, "cursor rounding bias 4K"),
    (0x32ef8, 0x9272c529, 0x9274cd29, "cursor length rounding mask 4K"),
    (0x32f64, 0x924036cb, 0x92402ecb, "cursor subpage offset 4K"),
    (0x33218, 0xd3727c55, 0xd3747c55, "cursor physical PFN shift 12"),
    (0x33230, 0x52880016, 0x52820016, "cursor range length 4K"),
    (0x33238, 0x52880003, 0x52820003, "cursor physical mapping length 4K"),
    (0x33270, 0x9140110b, 0x9140050b, "cursor adjacent page 4K"),
    (0x24050, 0xf240359f, 0xf2402d9f, "object list alignment 4K"),
    (0x24078, 0x5287ffea, 0x5281ffea, "object list rounding bias 4K"),
    (0x24080, 0x9272c54a, 0x9274cd4a, "object list rounding mask 4K"),
    (0x2409c, 0x5287ffe8, 0x5281ffe8, "placement list rounding bias 4K"),
    (0x240a4, 0x9272c508, 0x9274cd08, "placement list rounding mask 4K"),
    # PGMappedRangeTracker stores {startPage, pageCount} in a packed uint64.
    # Match the Intel donor's 12-bit shift AND its corresponding 20-bit
    # length packing; changing just validation would corrupt the run set.
    (0x28438, 0x92403448, 0x92402c48, "range tracker aligned length mask 4K"),
    (0x28440, 0xf240343f, 0xf2402c3f, "range tracker aligned offset mask 4K"),
    (0x28448, 0xd34eb428, 0xd34cac28, "range tracker start PFN shift 12"),
    (0x2844c, 0xaa024900, 0xaa025100, "range tracker length packing shift 20"),
    (0x284a4, 0x92727c28, 0x92747c28, "range tracker unaligned start mask 4K"),
    (0x284b0, 0x52bfff09, 0x52bffc09, "range tracker unaligned packing bias"),
    (0x284b4, 0x8b084920, 0x8b085120, "range tracker unaligned length shift 20"),
    (0x284b8, 0xb34eb420, 0xb34cac20, "range tracker unaligned PFN shift 12"),
    (0x295a0, 0xd372c501, 0xd374cd01, "range tracker unmapped offset shift 12"),
    (0x295a8, 0xd372c502, 0xd374cd02, "range tracker unmapped length shift 12"),
    (0x2985c, 0xd372c508, 0xd374cd08, "range tracker run start shift 12"),
    (0x29864, 0xd372c528, 0xd374cd28, "range tracker run length shift 12"),
    (0x3c778, 0x52880003, 0x52820003, "setRootPage: mapping length 16K -> 4K"),
    (0x3e180, 0xd3727c48, 0xd3747c48, "newContiguousMapping: PFN shift 14 -> 12"),
    (0x3e684, 0x7140107f, 0x7140047f, "physical read maximum length 4K"),
    (0x3e68c, 0x92403456, 0x92402c56, "physical read offset mask 4K"),
    (0x3e694, 0xf140111f, 0xf140051f, "physical read page-boundary check 4K"),
    (0x3e718, 0xd34efc42, 0xd34cfc42, "physical read fallback PFN shift 12"),
    (0x3e71c, 0x52880003, 0x52820003, "physical read fallback maps 4K"),
    (0x5b9e4, 0x52880003, 0x52820003, "child FIFO page list maps 4K"),
    (0x5ba0c, 0xf140051f, 0xf110011f, "child FIFO page list limit 1024 PFNs"),
    (0x5ba48, 0x52880009, 0x52820009, "child FIFO physical range length 4K"),
    (0x5ba54, 0xd372c56b, 0xd374cd6b, "child FIFO physical address shift 12"),
    (0x5ba70, 0x531246f4, 0x53144ef4, "child FIFO total length shift 12"),
    (0x3b530, 0xf240347f, 0xf2402c7f, "root FIFO length alignment 4K"),
    (0x2f9b4, 0x52880003, 0x52820003, "local task root mapping length 4K"),
    # setupTaskRoot capacity validation, paired with Intel 0x7ffb128dad13:
    # accept depths 1..4 and capacity 2^(12 + 10*depth), not ARM 1..3
    # and 2^(14 + 12*depth). This tightens validation; it does not explain
    # the currently empty depth-3 object table by itself.
    (0x2f9f0, 0x51001289, 0x51001689, "task root depth bound subtract 5"),
    (0x2f9f4, 0x31000d3f, 0x3100113f, "task root permits Intel depths 1 through 4"),
    (0x2fa18, 0xd374cd4a, 0xd376d54a, "task root capacity 10-bit table levels"),
    (0x2fa24, 0xeb0b691f, 0xeb0b591f, "task root capacity base exponent 22"),
    (0x40efc, 0x52880003, 0x52820003, "display shared-state page length 4K"),
    (0x45d30, 0xf240369f, 0xf2402e9f, "EFI framebuffer length alignment 4K"),
    (0x45f48, 0xd372c508, 0xd374cd08, "EFI framebuffer PFN shift 12"),
    (0x45f9c, 0x5287ffe8, 0x5281ffe8, "EFI byte-size round-up bias 4095"),
    (0x45fa4, 0x12124508, 0x12144d08, "EFI byte-size round-up mask 4K"),
    (0x4622c, 0xd372c508, 0xd374cd08, "EFI presentation physical address shift 12"),
    (0x3e2ac, 0xf240349f, 0xf2402c9f, "device range mapping length alignment 4K"),
]
for rva, before, after, description in patches:
    address = 0x100000000 + rva
    matches = [off + address - va for va, off, fs in segments
               if va <= address and address + 4 <= va + fs]
    if len(matches) != 1 or struct.unpack_from("<I", data, matches[0])[0] != before:
        raise SystemExit("Instruction guard failed: " + description)
    struct.pack_into("<I", data, matches[0], after)
    print(description)
# Never overwrite a donor or previous candidate. Caller ad-hoc signs the copy.
with a.output.open("xb") as out:
    out.write(data)
