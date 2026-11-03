"""Bounded source-level metadata in an exact owned-driver guest, no calls.

vim-client-profile arm|arm-backing|stop|read EXPECTED_UUID [milliseconds]
Arm refuses active/inflight work. Stop disables new records; if another call
was stopped mid-span, detach/resume and read later once inflight reaches zero.
No rendering state, clock, host process, launch environment or disk is changed.
"""
import json
import lldb
import struct

ROWS = 1231
SIZE = 48 + 40 * (2 * ROWS + 7)


def command(debugger, text, result, internal_dict):
    parts = text.split()
    if len(parts) not in (2, 3) or parts[0] not in ('arm', 'arm-backing', 'stop', 'read'):
        result.SetError('Use vim-client-profile arm|arm-backing|stop|read EXPECTED_UUID [milliseconds]')
        return
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    if target.GetExecutable().GetFilename() != 'WindowServer' or process.GetState() != lldb.eStateStopped:
        result.SetError('Stop the OWNED-driver guest WindowServer first; never attach the physical reference.')
        return
    modules = [target.GetModuleAtIndex(i) for i in range(target.GetNumModules())
               if target.GetModuleAtIndex(i).GetFileSpec().GetFilename() == 'libVIMPassthroughOpenGL.dylib']
    wanted = parts[1].replace('-', '').upper()
    if len(modules) != 1 or (modules[0].GetUUIDString() or '').replace('-', '').upper() != wanted:
        result.SetError('Refused unmatched research backend UUID; no write.')
        return
    module = modules[0]

    def address(name):
        entries = module.FindSymbols(name)
        found = set()
        for i in range(entries.GetSize()):
            at = entries.GetContextAtIndex(i).GetSymbol().GetStartAddress().GetLoadAddress(target)
            if at != lldb.LLDB_INVALID_ADDRESS:
                found.add(at)
        if len(found) != 1:
            raise RuntimeError('Missing/ambiguous owned symbol: ' + name)
        return next(iter(found))

    def read(at, length):
        chunks = []
        for offset in range(0, length, 16384):
            n = min(16384, length - offset)
            error = lldb.SBError()
            data = process.ReadMemory(at + offset, n, error)
            if error.Fail() or len(data) != n:
                raise RuntimeError('Cannot read complete owned metadata.')
            chunks.append(data)
        return b''.join(chunks)

    def write(at, data):
        error = lldb.SBError()
        if process.WriteMemory(at, data, error) != len(data) or error.Fail() or read(at, len(data)) != data:
            raise RuntimeError('Metadata write/readback failed; inspect before resuming.')

    try:
        active_at = address('dg_client_profile_active')
        active = struct.unpack('<Q', read(active_at, 8))[0]
        inflight = struct.unpack('<I', read(address('dg_client_profile_inflight'), 4))[0]
        if parts[0] == 'stop':
            write(active_at, struct.pack('<Q', 0))
            result.PutCString(json.dumps(dict(Enabled=False, Inflight=inflight,
                Next='Detach and resume before read if Inflight is nonzero.')))
            return
        if active or inflight:
            raise RuntimeError('Active/inflight interval; stop, detach/resume, then read or arm. No reset performed.')
        report_at = address('dg_client_profile_report')
        if parts[0] in ('arm', 'arm-backing'):
            milliseconds = int(parts[2]) if len(parts) == 3 else 10000
            if not 1 <= milliseconds <= 60000:
                raise RuntimeError('Use a duration between 1 and 60000 ms.')
            serial_at = address('dg_client_profile_serial')
            serial = struct.unpack('<Q', read(serial_at, 8))[0]
            if serial == (1 << 64) - 1:
                raise RuntimeError('Generation exhausted; refused.')
            mode_symbols = module.FindSymbols('dg_client_profile_backing_only')
            if parts[0] == 'arm-backing' and not mode_symbols.GetSize():
                raise RuntimeError('This exact client has no backing-only mode; no reset performed.')
            if mode_symbols.GetSize():
                write(address('dg_client_profile_backing_only'), struct.pack('<I', int(parts[0] == 'arm-backing')))
            write(report_at, b'\0' * SIZE)
            if module.FindSymbols('dg_client_backing_slow_count').GetSize():
                write(address('dg_client_backing_slow_count'), struct.pack('<I', 0))
                write(address('dg_client_backing_slow'), b'\0' * (32 * 32))
            write(address('dg_client_profile_ms'), struct.pack('<I', milliseconds))
            write(serial_at, struct.pack('<Q', serial + 1))
            write(active_at, struct.pack('<Q', serial + 1))
            result.PutCString(json.dumps(dict(Enabled=True, Generation=serial + 1, Milliseconds=milliseconds,
                BackingOnly=parts[0] == 'arm-backing')))
            return
        data = read(report_at, SIZE)
        version, numer, denom, reserved, generation, started, ended, excluded = struct.unpack_from('<IIIIQQQQ', data)
        if version != 1 or not numer or not denom or ended < started or reserved:
            raise RuntimeError('Incomplete/unsupported profile; no measurements inferred.')
        scale = float(numer) / denom / 1000.0
        groups = dict(Async=[], Sync=[], Transfers=[], Pointers=[], Draws=[], Locks=[])
        offset = 48
        for group, count in (('Async', ROWS), ('Sync', ROWS), ('Transfers', 2), ('Pointers', 3), ('Draws', 1), ('Locks', 1)):
            for i in range(count):
                entered, completed, ticks, maximum, size = struct.unpack_from('<QQQQQ', data, offset)
                offset += 40
                if not entered:
                    continue
                key = i if group not in ('Async', 'Sync') or i < 974 else (0xffffffff if i == ROWS - 1 else 0x10000 + i - 974)
                groups[group].append(dict(Op=key, Entered=entered, Completed=completed,
                    Microseconds=ticks * scale, MaximumMicroseconds=maximum * scale, Bytes=size))
        slow = []
        if module.FindSymbols('dg_client_backing_slow_count').GetSize():
            count = struct.unpack('<I', read(address('dg_client_backing_slow_count'), 4))[0]
            if count > 32:
                raise RuntimeError('Invalid bounded backing-span count; no measurements inferred.')
            records = read(address('dg_client_backing_slow'), count * 32)
            for i in range(count):
                serial, op, first, last = struct.unpack_from('<QQQQ', records, i * 32)
                if serial != generation or last < first or op not in (0x100f4, 0x100f5, 0x100f6):
                    raise RuntimeError('Invalid backing-span generation/clock/op; no measurements inferred.')
                slow.append(dict(Op=op, BeginMachSeconds=first*scale/1e6,
                    EndMachSeconds=last*scale/1e6, Microseconds=(last-first)*scale))
        result.PutCString(json.dumps(dict(Generation=generation, DurationMilliseconds=(ended-started)*scale/1000,
            Excluded=excluded, Inflight=0, Caution='Nested instrumented wall spans, not CPU/GPU time or clean FPS.',
            Groups=groups, SlowBackingSpans=slow), sort_keys=True))
    except (RuntimeError, ValueError) as failure:
        result.SetError(str(failure))


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand('command script add -f ' + __name__ + '.command vim-client-profile')
