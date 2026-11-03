#!/usr/bin/env python3
"""Bracket guest Mach time against host Mach time through an existing SSH master.

Read-only, no QMP/QGA connection, TCC change or guest instrumentation. Bounds
include the entire SSH/Python round trip; never call the midpoint exact input
latency. Recheck bounds after a reboot. DTrace's timestamp clock must separately
be qualified against guest Mach time before joining a trace to host VM-exit logs.
"""
import argparse
import ctypes
import json
from pathlib import Path
import subprocess


class Timebase(ctypes.Structure):
    _fields_ = [('numer', ctypes.c_uint32), ('denom', ctypes.c_uint32)]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--ssh-control', type=Path, required=True)
    p.add_argument('--port', type=int, default=2222)
    a = p.parse_args()
    if not a.ssh_control.is_socket() or not 1 <= a.port <= 65535:
        p.error('Use the actual guest SSH control socket and valid forwarded port')
    lib = ctypes.CDLL('/usr/lib/libSystem.B.dylib')
    lib.mach_absolute_time.restype = ctypes.c_uint64
    scale = Timebase()
    if lib.mach_timebase_info(ctypes.byref(scale)) or not scale.denom:
        raise RuntimeError('Host Mach timebase unavailable')

    def clock():
        return lib.mach_absolute_time() * scale.numer // scale.denom

    guest = '''import ctypes as c,json
class T(c.Structure):
 _fields_=[('numer',c.c_uint32),('denom',c.c_uint32)]
l=c.CDLL('/usr/lib/libSystem.B.dylib');l.mach_absolute_time.restype=c.c_uint64
t=T();assert not l.mach_timebase_info(c.byref(t)) and t.denom
print(json.dumps(dict(GuestMachNanoseconds=l.mach_absolute_time()*t.numer//t.denom)))
'''
    rows = []
    for _ in range(5):
        before = clock()
        result = subprocess.run(['ssh', '-S', str(a.ssh_control), '-p', str(a.port),
                                 '-o', 'BatchMode=yes', 'mac@127.0.0.1', '/usr/bin/python -'],
                                input=guest, text=True, capture_output=True, check=True)
        after = clock()
        value = json.loads(result.stdout)['GuestMachNanoseconds']
        rows.append(dict(HostBeforeNS=before, HostAfterNS=after, GuestMachNS=value,
                         MinimumOffsetNS=before-value, MaximumOffsetNS=after-value))
    lower = max(row['MinimumOffsetNS'] for row in rows)
    upper = min(row['MaximumOffsetNS'] for row in rows)
    if lower > upper:
        raise RuntimeError('Clock brackets do not intersect; do not correlate these clocks')
    print(json.dumps(dict(Rows=rows, HostMinusGuestOffsetNS=[lower, upper],
                         UncertaintyNS=upper-lower, NotExactInputLatency=True), indent=2))


if __name__ == '__main__':
    main()
