#!/usr/bin/env python
"""Guarded native Mojave GPU detection patch, without UUID/address pins.

Resolve RIP-relative operands and validate the complete vendor/mask predicates.
Unknown layouts fail closed. Accepts universal images; Python 2.7/3 compatible.
"""
from __future__ import print_function
import argparse
from pvg_macho import Image, one, relative, require, save


def patch(source, output):
    with open(source, 'rb') as stream:
        image = Image(stream.read())
    predicate = (b'\x80\x3d....\x00\x74\x51\x48\x83\x3d....\xff\x75\x4a'
                 b'\x0f\xb6\x05....\x89\xc2\x83\xca\x02\x31\xc9\x38\x0d....'
                 b'\x0f\x44\xd0\x89\xd0\x83\xc8\x04\x38\x0d....\x8b\x0d....'
                 b'\x0f\x44\xc2\x85\xc9\x74\x11\x21\xc8\x39\xc8\x75\x0b'
                 b'\x80\x3d....\x00\x0f\x95\xc0\xc3')
    all_gates = image.matches(predicate)
    block = b'\x48\x8d\x3d....\xe8....\x84\xc0\x74\x07\x80\x0d....'
    detector = b'\x55\x48\x89\xe5' + block+b'\x01'+block+b'\x02'+block+b'\x04'
    sites = image.matches(detector+b'\x5d\xc3')
    def correlated(sites):
        return [s for s in sites if sum(image.rel(p+53, 2, 6) == image.rel(s+20, 2, 7)
                                       for p in all_gates) == 2]
    sites = correlated(sites)
    old = False
    if not sites:
        first = min(s[4] for s in image.sections if s[0] == b'__TEXT' and s[4])
        base = one([s[1] for s in image.segments if s[0] == b'__TEXT'], 'text segment')
        sites = image.matches(detector, (base+image.command_end, base+first))
        old = True
    sites = correlated(sites)
    start = one(sites, 'GPU detector correlated with capability predicates')
    classes, helpers, globals_ = [], [], []
    for n, (name, bit) in enumerate(((b'AppleIntelFramebuffer', 1), (b'NVDA', 2), (b'AMDFramebuffer', 4))):
        p = start+4+23*n
        string = image.rel(p, 3, 7)
        require(image.read(string, len(name)+1) == name+b'\0', 'vendor string mismatch')
        classes.append((string, bit))
        helpers.append(image.rel(p+7, 1, 5))
        globals_.append(image.rel(p+16, 2, 7))
    require(len(set(helpers)) == len(set(globals_)) == 1, 'inconsistent GPU detector references')
    helper, gpu = helpers[0], globals_[0]
    require(image.text[2] <= helper < image.text[2]+image.text[3], 'helper outside text')
    image.writable(gpu)
    gates = [p for p in all_gates if image.rel(p+53, 2, 6) == gpu]
    require(len(gates) == 2, 'expected exactly two correlated capability predicates')
    masks = sorted(image.rel(p+19, 3, 7) for p in gates)
    require(len(set(masks)) == 2, 'duplicate GPU mask')
    for address in masks:
        image.writable(address)
    cave = start if old else image.cave(256)
    code = b'\x55\x48\x89\xe5'
    for string, bit, targets in [(s, b, [gpu]) for s, b in classes] + [(cave+192, 16, [gpu]+masks)]:
        code += relative(b'\x48\x8d\x3d', cave+len(code), string)
        code += relative(b'\xe8', cave+len(code), helper)
        code += b'\x84\xc0\x74' + bytes(bytearray([7*len(targets)]))
        for target in targets:
            code += relative(b'\x80\x0d', cave+len(code), target, bytes(bytearray([bit])))
    code += b'\x5d\xc3'
    require(len(code) < 192, 'trampoline too large')
    label = b'AppleParavirtAccelerator\0'
    data = bytearray(image.data)
    if old:
        require(image.read(cave, len(code)) == code, 'unknown existing detector trampoline')
        require(image.read(cave+192, len(label)) == label, 'unknown detector label')
        one([p for p in image.matches(b'\xe9....\x90{6}') if image.rel(p, 1, 5) == cave], 'patched detector entry')
    else:
        p = image.offset(cave, 256)
        data[p:p+len(code)] = code
        data[p+192:p+192+len(label)] = label
        p = image.offset(start, 11)
        data[p:p+11] = relative(b'\xe9', start, cave)+b'\x90'*6
    save(output, image.preserving_slices(data))
    print('CoreDisplay: additive PVG detection; native GPU paths and enable flags preserved')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input'); parser.add_argument('output')
    args = parser.parse_args()
    patch(args.input, args.output)
