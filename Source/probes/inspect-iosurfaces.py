#!/usr/bin/python
"""Inspect globally named IOSurfaces without modifying their contents.

This deliberately targets the Python 2 runtime shipped by Catalina and Big Sur.
Run as the logged-in GUI user (or root) and pass surface IDs, or a range such as
``1-16``.  The digest is diagnostic: it identifies changing/displayed buffers,
not a provenance checksum.
"""

from __future__ import print_function

import ctypes
import hashlib
import sys


IOSURFACE_PATH = "/System/Library/Frameworks/IOSurface.framework/IOSurface"
COREFOUNDATION_PATH = "/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation"
READ_ONLY = 1


def configure():
    ios = ctypes.CDLL(IOSURFACE_PATH)
    cf = ctypes.CDLL(COREFOUNDATION_PATH)

    ios.IOSurfaceLookup.argtypes = [ctypes.c_uint32]
    ios.IOSurfaceLookup.restype = ctypes.c_void_p
    for name in ("IOSurfaceGetWidth", "IOSurfaceGetHeight",
                 "IOSurfaceGetBytesPerRow", "IOSurfaceGetAllocSize"):
        function = getattr(ios, name)
        function.argtypes = [ctypes.c_void_p]
        function.restype = ctypes.c_size_t
    ios.IOSurfaceGetPixelFormat.argtypes = [ctypes.c_void_p]
    ios.IOSurfaceGetPixelFormat.restype = ctypes.c_uint32
    ios.IOSurfaceGetBaseAddress.argtypes = [ctypes.c_void_p]
    ios.IOSurfaceGetBaseAddress.restype = ctypes.c_void_p
    ios.IOSurfaceLock.argtypes = [ctypes.c_void_p, ctypes.c_uint32,
                                  ctypes.POINTER(ctypes.c_uint32)]
    ios.IOSurfaceLock.restype = ctypes.c_int
    ios.IOSurfaceUnlock.argtypes = [ctypes.c_void_p, ctypes.c_uint32,
                                    ctypes.POINTER(ctypes.c_uint32)]
    ios.IOSurfaceUnlock.restype = ctypes.c_int
    cf.CFRelease.argtypes = [ctypes.c_void_p]
    cf.CFRelease.restype = None
    return ios, cf


def parse_ids(arguments):
    if not arguments:
        return range(1, 33)
    result = []
    for value in arguments:
        if "-" in value:
            first, last = [int(part, 0) for part in value.split("-", 1)]
            result.extend(range(first, last + 1))
        else:
            result.append(int(value, 0))
    return result


def fourcc(value):
    raw = "".join(chr((value >> shift) & 0xff) for shift in (24, 16, 8, 0))
    return "".join(character if 32 <= ord(character) < 127 else "."
                   for character in raw)


def inspect(ios, cf, surface_id):
    surface = ios.IOSurfaceLookup(surface_id)
    if not surface:
        return None
    seed = ctypes.c_uint32(0)
    locked = False
    try:
        width = ios.IOSurfaceGetWidth(surface)
        height = ios.IOSurfaceGetHeight(surface)
        row_bytes = ios.IOSurfaceGetBytesPerRow(surface)
        allocation_size = ios.IOSurfaceGetAllocSize(surface)
        pixel_format = ios.IOSurfaceGetPixelFormat(surface)
        lock_result = ios.IOSurfaceLock(surface, READ_ONLY, ctypes.byref(seed))
        locked = lock_result == 0
        address = ios.IOSurfaceGetBaseAddress(surface) if locked else 0
        digest = "-"
        nonzero = -1
        samples = "-"
        if address and allocation_size:
            pixels = ctypes.string_at(address, allocation_size)
            digest = hashlib.sha256(pixels).hexdigest()[:16]
            nonzero = allocation_size - pixels.count("\0")
            offsets = sorted(set((0, allocation_size // 4,
                                  allocation_size // 2,
                                  (allocation_size * 3) // 4,
                                  max(0, allocation_size - 16))))
            samples = ",".join(pixels[offset:offset + 16].encode("hex")
                               for offset in offsets)
        return (width, height, row_bytes, allocation_size, pixel_format,
                lock_result, seed.value, address or 0, nonzero, digest, samples)
    finally:
        if locked:
            ios.IOSurfaceUnlock(surface, READ_ONLY, ctypes.byref(seed))
        cf.CFRelease(surface)


def main():
    ios, cf = configure()
    print("id width height bpr size format lock seed base nonzero sha256/16 samples")
    found = 0
    for surface_id in parse_ids(sys.argv[1:]):
        details = inspect(ios, cf, surface_id)
        if details is None:
            continue
        found += 1
        width, height, row_bytes, allocation_size, pixel_format, lock_result, seed, address, nonzero, digest, samples = details
        print("%u %u %u %u %u 0x%08x(%s) %d %u 0x%x %d %s %s" %
              (surface_id, width, height, row_bytes, allocation_size,
               pixel_format, fourcc(pixel_format), lock_result, seed, address,
               nonzero, digest, samples))
    print("found=%u" % found)
    return 0


if __name__ == "__main__":
    sys.exit(main())
