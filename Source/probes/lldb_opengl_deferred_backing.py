"""Explicit same-boot owned-driver experiment, no inferior function calls.

vim-deferred-backing status|on|future|writer|off EXPECTED_CLIENT_UUID
Only an attached, stopped research WindowServer with exactly one matching
libVIMPassthroughOpenGL.dylib may be changed. No launch environment or disk patch.
Counters are engineering admissions, NOT presented frames/FPS/latency.
"""
import json
import lldb
import struct


def command(debugger, text, result, internal_dict):
    parts = text.split()
    if len(parts) != 2 or parts[0] not in ('on', 'future', 'writer', 'off', 'status'):
        result.SetError('Use vim-deferred-backing status|on|future|writer|off EXPECTED_CLIENT_UUID')
        return
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    if target.GetExecutable().GetFilename() != 'WindowServer' or process.GetState() != lldb.eStateStopped:
        result.SetError('Attach and stop the OWNED-driver guest WindowServer first.')
        return
    wanted = parts[1].replace('-', '').upper()
    modules = [target.GetModuleAtIndex(i) for i in range(target.GetNumModules())
               if target.GetModuleAtIndex(i).GetFileSpec().GetFilename() == 'libVIMPassthroughOpenGL.dylib']
    if len(modules) != 1 or (modules[0].GetUUIDString() or '').replace('-', '').upper() != wanted:
        result.SetError('Refused unmatched research client; no write.')
        return
    module = modules[0]

    def address(name):
        entries = module.FindSymbols(name)
        found = set()
        for index in range(entries.GetSize()):
            symbol = entries.GetContextAtIndex(index).GetSymbol()
            value = symbol.GetStartAddress().GetLoadAddress(target)
            if value != lldb.LLDB_INVALID_ADDRESS:
                found.add(value)
        if len(found) != 1:
            raise RuntimeError('Missing/ambiguous local research symbol: ' + name)
        return next(iter(found))

    def read(at, length):
        error = lldb.SBError()
        value = process.ReadMemory(at, length, error)
        if error.Fail() or len(value) != length:
            raise RuntimeError('Cannot read qualified research state.')
        return value

    try:
        flag = address('dg_deferred_backing_scopes')
        current = struct.unpack('<I', read(flag, 4))[0]
        future_address = (address('dg_future_backing_scopes') if
                          module.FindSymbols('dg_future_backing_scopes').GetSize() else None)
        future = struct.unpack('<I', read(future_address, 4))[0] if future_address is not None else None
        writer_address = (address('dg_writer_backing_scopes') if
                          module.FindSymbols('dg_writer_backing_scopes').GetSize() else None)
        writer = struct.unpack('<I', read(writer_address, 4))[0] if writer_address is not None else None
        if current not in (0, 1):
            raise RuntimeError('Unexpected flag state; refused.')
        if future is not None and future not in (0, 1):
            raise RuntimeError('Unexpected future flag state; refused.')
        if writer is not None and writer not in (0, 1):
            raise RuntimeError('Unexpected writer flag state; refused.')
        if parts[0] in ('future', 'writer') and future_address is None:
            raise RuntimeError('This exact client has no independent future flag; refused.')
        if parts[0] == 'writer' and writer_address is None:
            raise RuntimeError('This exact client has no independent writer flag; refused.')
        if parts[0] != 'status':
            expected = 0 if parts[0] == 'off' else 1
            if writer_address is not None:
                writer = int(parts[0] == 'writer')
                error = lldb.SBError()
                wanted_writer = struct.pack('<I', writer)
                if process.WriteMemory(writer_address, wanted_writer, error) != 4 or error.Fail() or read(writer_address, 4) != wanted_writer:
                    raise RuntimeError('Writer flag write/readback failed; inspect before resuming.')
            if future_address is not None:
                future = int(parts[0] in ('future', 'writer'))
                error = lldb.SBError()
                wanted_future = struct.pack('<I', future)
                if process.WriteMemory(future_address, wanted_future, error) != 4 or error.Fail() or read(future_address, 4) != wanted_future:
                    raise RuntimeError('Future flag write/readback failed; inspect before resuming.')
            error = lldb.SBError()
            if process.WriteMemory(flag, struct.pack('<I', expected), error) != 4 or error.Fail():
                raise RuntimeError('Flag write failed; inspect before resuming.')
            current = struct.unpack('<I', read(flag, 4))[0]
            if current != expected:
                raise RuntimeError('Flag readback failed; inspect before resuming.')
        counters = struct.unpack('<QQQQ', read(address('dg_deferred_backing_stats'), 32))
        state = dict(Enabled=bool(current), FutureEnabled=future, WriterEnabled=writer, Attempts=counters[0],
            Begins=counters[1], PendingCommits=counters[2], SynchronousFallbacks=counters[3])
        if writer_address is not None:
            writers = struct.unpack('<QQQQ', read(address('dg_writer_backing_stats'), 32))
            state.update(WriterClaims=writers[0], WriterContinuations=writers[1],
                         WriterRefusals=writers[2], WriterInitialGates=writers[3])
        result.PutCString(json.dumps(state, sort_keys=True))
    except RuntimeError as failure:
        result.SetError(str(failure))


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand('command script add -f ' + __name__ + '.command vim-deferred-backing')
