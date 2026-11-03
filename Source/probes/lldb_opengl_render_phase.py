"""Exact-UUID, transient VBL-phase experiment; NOT an install/runtime policy.

Native MPGetNextWakeupTime computes lastVBL + period + arg*(period>>2), then
advances one period only if past. Change only begin_deferred_update_cycle's
existing -3 argument (quarter phase) to -4 (VBL boundary, <=3 tick rounding).
All native branches, first-transaction admission and clocks remain in place.
No global open-loop flag, forced/triple updates, pixel/ownership change, new
clock, scratch code allocation, inferior call or OS/host file modification.
Keep OFF unless BOTH first-admission batching and real presentation improve.
"""
import json
import lldb
import struct
import binascii

CORE = 'ED5E3C377E2F38858091DFC9B44B38E8'


def command(debugger, text, result, internal_dict):
    args = text.split()
    if len(args) != 2 or args[0] not in ('boundary', 'quarter', 'status'):
        result.SetError('Use vim-render-phase boundary|quarter|status EXPECTED_BACKEND_UUID')
        return
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    if target.GetExecutable().GetFilename() != 'WindowServer' or process.GetState() != lldb.eStateStopped:
        result.SetError('Stop the OWNED-driver guest WindowServer, never the physical reference.')
        return

    def symbol(name, module_name, uuid):
        entries = target.FindSymbols(name)
        values = set()
        for i in range(entries.GetSize()):
            e = entries.GetContextAtIndex(i)
            m = e.GetModule()
            if m.GetFileSpec().GetFilename() != module_name or (m.GetUUIDString() or '').replace('-', '').upper() != uuid:
                continue
            a = e.GetSymbol().GetStartAddress().GetLoadAddress(target)
            if a != lldb.LLDB_INVALID_ADDRESS:
                values.add(a)
        if len(values) != 1:
            raise RuntimeError('Missing/ambiguous qualified symbol: ' + name)
        return next(iter(values))

    def read(at, size):
        error = lldb.SBError()
        data = process.ReadMemory(at, size, error)
        if error.Fail() or len(data) != size:
            raise RuntimeError('Cannot read complete qualified code/state.')
        return data

    try:
        wanted = args[1].replace('-', '').upper()
        active = symbol('dg_client_profile_active', 'libVIMPassthroughOpenGL.dylib', wanted)
        if read(active, 8) != b'\0' * 8:
            raise RuntimeError('Stop source profiling before changing scheduling.')
        begin = symbol('begin_deferred_update_cycle', 'CoreGraphics', CORE)
        wake = symbol('CGXGetNextWakeupTime', 'CoreGraphics', CORE)
        native = symbol('MPGetNextWakeupTime', 'CoreGraphics', CORE)
        # Proved native integer math, not a guessed meaning for negative args.
        if read(native + 0xad, 20) != binascii.unhexlify('4963d64889f048c1e802480fafc24801f04801f8'):
            raise RuntimeError('Unmatched native phase arithmetic; no write.')
        at = begin + 0xca
        code = read(at, 5)
        original, patched = b'\xbe\xfd\xff\xff\xff', b'\xbe\xfc\xff\xff\xff'
        if code not in (original, patched) or read(at + 5, 5) != b'\xe8' + struct.pack('<i', wake - (at + 10)):
            raise RuntimeError('Unmatched native argument/call site; no write.')
        flags = {}
        for name in ('gDeferredUpdates', 'gDeferredUpdatesPref', 'gOpenLoopScheduler',
                     'gForceDeferredUpdates', 'gTripleBufferUpdates'):
            flags[name] = struct.unpack('B', read(symbol(name, 'CoreGraphics', CORE), 1))[0]
        if args[0] == 'boundary' and flags != dict(gDeferredUpdates=1, gDeferredUpdatesPref=1,
                gOpenLoopScheduler=0, gForceDeferredUpdates=0, gTripleBufferUpdates=0):
            raise RuntimeError('Changed native batching policy; no write.')
        if args[0] == 'boundary' and read(symbol('CGXUpdateDisplay', 'CoreGraphics', CORE) + 0x276d, 6) != b'\x90' * 6:
            raise RuntimeError('Require the already-qualified scheduled-period control first; no write.')
        if args[0] != 'status':
            desired = patched if args[0] == 'boundary' else original
            error = lldb.SBError()
            if process.WriteMemory(at + 1, desired[1:2], error) != 1 or error.Fail() or read(at, 5) != desired:
                process.WriteMemory(at + 1, original[1:2], error)
                raise RuntimeError('Phase write/readback failed; inspect restoration before resuming.')
            code = desired
        result.PutCString(json.dumps(dict(Phase='VBL boundary' if code == patched else 'quarter',
            Address=hex(at + 1), Flags=flags, Transient=True,
            Next='Detach AND quit; require native first-admission batching and real visual/pacing acceptance.')))
    except (RuntimeError, struct.error) as failure:
        result.SetError(str(failure))


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand('command script add -f ' + __name__ + '.command vim-render-phase')
