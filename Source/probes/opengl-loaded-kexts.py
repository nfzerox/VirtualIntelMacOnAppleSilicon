#!/usr/bin/python
"""Read actual loaded Mavericks kext UUIDs, not the files currently on disk.

Uses the period OS's Python/ctypes and public KextManager API. Read-only;
run through SSH stdin with /usr/bin/python - (no guest dev tools needed).
"""
from __future__ import print_function
import ctypes as c
import plistlib
import uuid

iokit = c.CDLL('/System/Library/Frameworks/IOKit.framework/IOKit')
core = c.CDLL('/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation')
iokit.KextManagerCopyLoadedKextInfo.argtypes = [c.c_void_p, c.c_void_p]
iokit.KextManagerCopyLoadedKextInfo.restype = c.c_void_p
core.CFPropertyListCreateData.argtypes = [c.c_void_p, c.c_void_p, c.c_long, c.c_ulong, c.c_void_p]
core.CFPropertyListCreateData.restype = c.c_void_p
core.CFDataGetLength.argtypes = [c.c_void_p]
core.CFDataGetLength.restype = c.c_long
core.CFDataGetBytePtr.argtypes = [c.c_void_p]
core.CFDataGetBytePtr.restype = c.c_void_p
core.CFRelease.argtypes = [c.c_void_p]
info = iokit.KextManagerCopyLoadedKextInfo(None, None)
assert info
data = core.CFPropertyListCreateData(None, info, 100, 0, None)
assert data
loaded = plistlib.readPlistFromString(c.string_at(core.CFDataGetBytePtr(data), core.CFDataGetLength(data)))
for name in ('net.osx86.driver.VMsvga2', 'net.osx86.driver.VMsvga2Accel'):
    value = loaded[name]['OSBundleUUID'].data
    print(name, str(uuid.UUID(bytes=value)).upper())
core.CFRelease(data)
core.CFRelease(info)
