#!/usr/bin/env python
"""Patch the guest's loose SkyLight, using symbols and verified branch targets.

No UUID, preferred address or function-offset pins. Unknown layouts fail closed.
Preserves native compositor capability checks and OpenGL success reporting.
"""
from __future__ import print_function
import argparse
import struct
from pvg_macho import Image, one, relative, require, save


def patch(source, destination, metal_capability_report=False):
    with open(source, 'rb') as stream:
        image = Image(stream.read())
    bounds = image.function('_CGXInitializeGL')
    select = image.symbol('__ZL27activate_compositor_defaultv')
    once = image.symbol('__ZZ15WSGVAInitializeE4once')
    image.function('__ZL27activate_compositor_defaultv')
    image.writable(once)
    calls = [p for p in image.matches(b'\xe8....', bounds) if image.rel(p, 1, 5) == select]
    common_sites = [p for p in image.matches(b'\x48\x83\x3d....\xff\x75.', bounds)
                    if image.rel(p, 3, 8) == once]
    data = bytearray(image.data)
    def put(address, code):
        p = image.offset(address, len(code)); data[p:p+len(code)] = code
    old = not calls
    if old:
        # Validate the complete legacy/new common-exit trampoline, not merely
        # the absence of the original call. This permits safe reinstall.
        matches = []
        for p in image.matches(b'\xe9....\x90{3}\x75.', bounds):
            c = image.rel(p, 1, 5)
            expected = relative(b'\xe8', c, select)
            expected += relative(b'\x48\x83\x3d', c+5, once, b'\xff')
            expected += relative(b'\xe9', c+13, p+8)
            try:
                if image.read(c, len(expected)) == expected:
                    matches.append((p, c))
            except ValueError:
                pass
        common, cave = one(matches, 'existing compositor trampoline')
        require(not common_sites, 'mixed patched/unpatched compositor sites')
    else:
        success = one(calls, 'default compositor call')
        common = one(common_sites, 'common exit guard')
        require(success < common, 'unexpected compositor control flow')
        # Existing branches must enter this shared exit (including the GL
        # failure paths); do not move a call into an arbitrary once guard.
        jumps = [p for p in image.matches(b'\xe9....', bounds) if image.rel(p, 1, 5) == common]
        jumps += [p for p in image.matches(b'\x0f[\x80-\x8f]....', bounds)
                  if image.rel(p, 2, 6) == common]
        require(jumps, 'common exit has no incoming branch')
        cave = image.cave(128)
        code = relative(b'\xe8', cave, select)
        code += relative(b'\x48\x83\x3d', cave+len(code), once, b'\xff')
        code += relative(b'\xe9', cave+len(code), common+8)
        put(cave, code); put(success, b'\x90'*5)
        put(common, relative(b'\xe9', common, cave)+b'\x90'*3)
    if metal_capability_report:
        report_bounds = image.function('__XServerMightHaveAcceleration')
        gl = image.symbol('__cglsContext')
        metal = image.symbol('_WSMetalCompositorEnabled')
        image.writable(gl); image.function('_WSMetalCompositorEnabled')
        reports = [p for p in image.matches(b'\x48\x83\x3d....\x00\x0f\x95\x46\\$', report_bounds)
                   if image.rel(p, 3, 8) == gl]
        rc = cave+32
        def report_code(report):
            code = relative(b'\x48\x83\x3d', rc, gl, b'\x00') + b'\x75\x11\x57\x56'
            code += relative(b'\xe8', rc+len(code), metal) + b'\x5e\x5f\x88\x46\x24'
            code += relative(b'\xe9', rc+len(code), report+12) + b'\xc6\x46\x24\x01'
            return code + relative(b'\xe9', rc+len(code), report+12)
        if reports:
            report = one(reports, 'acceleration report')
            require(image.read(rc, 48) == b'\0'*48, 'report trampoline padding occupied')
            put(rc, report_code(report))
            put(report, relative(b'\xe9', report, rc)+b'\x90'*7)
        else:
            require(old, 'unknown acceleration report')
            report = one([p for p in image.matches(b'\xe9....\x90{7}', report_bounds)
                          if image.rel(p, 1, 5) == rc], 'existing acceleration report')
            expected = report_code(report)
            require(image.read(rc, len(expected)) == expected, 'unknown acceleration trampoline')
    cursor, count = 32, 0
    old_name = b'/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit'
    new_name = b'/usr/lib/PVGGLIOKit.dylib'
    for _ in range(struct.unpack_from('<I', data, 16)[0]):
        command, size = struct.unpack_from('<II', data, cursor)
        require(size >= 8 and cursor + size <= len(data), 'invalid dependency command')
        if command == 12:
            require(size >= 24, 'short dependency command')
            offset = struct.unpack_from('<I', data, cursor + 8)[0]
            require(24 <= offset < size, 'invalid dependency name')
            start, end = cursor + offset, cursor + size
            name = bytes(data[start:end]).split(b'\0')[0]
            if name in (old_name, new_name, b'@loader_path/PVGGLIOKit.dylib'):
                require(len(new_name) < end-start, 'insufficient Surface2 dependency space')
                data[start:end] = new_name.ljust(end-start, b'\0')
                count += 1
        cursor += size
    require(count == 1, 'expected one IOKit/Surface2 dependency')
    save(destination, image.preserving_slices(data))
    print('SkyLight: validated native compositor startup and capability reporting')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source'); parser.add_argument('destination')
    parser.add_argument('--metal-capability-report', action='store_true')
    args = parser.parse_args()
    patch(args.source, args.destination, args.metal_capability_report)
