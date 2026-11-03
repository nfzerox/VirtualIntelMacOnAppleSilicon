"""Owned candidate only: transient staged-import A/B, no inferior calls.

The candidate preserves source exclusion/input snapshot ownership and actual
publication completion. It never authorizes skipping a fence on the old copy
path. Exact compiled UUID/symbol/current-value guards are mandatory. Detach
before driving or timing transitions. Normal executors contain no such flag.
"""
import json
import struct
import lldb

def address(target, name, expected_uuid):
    matches = target.FindSymbols(name)
    found = set()
    for i in range(matches.GetSize()):
        item = matches.GetContextAtIndex(i)
        module = item.GetModule()
        if module.GetFileSpec().GetFilename() != 'libVIMPassthroughOpenGLHost.dylib':
            continue
        if (module.GetUUIDString() or '').replace('-', '').upper() != expected_uuid:
            raise RuntimeError('Refused unmatched candidate executor UUID')
        value = item.GetSymbol().GetStartAddress().GetLoadAddress(target)
        if value != lldb.LLDB_INVALID_ADDRESS:
            found.add(value)
    if len(found) != 1:
        raise RuntimeError('Expected one exact candidate scalar: '+name)
    return next(iter(found))


def read(process, pointer, size):
    error = lldb.SBError()
    value = process.ReadMemory(pointer, size, error)
    if error.Fail() or len(value) != size:
        raise RuntimeError('Cannot read candidate scalar')
    return value


def command(debugger, text, result, internal_dict):
    try:
        parts = text.split()
        if len(parts) != 2 or parts[0] not in ('on', 'off', 'status'):
            raise RuntimeError('Use vim-staged-restore on|off|status EXPECTED_EXECUTOR_UUID')
        mode, expected_uuid = parts[0], parts[1].replace('-', '').upper()
        if len(expected_uuid) != 32 or any(c not in '0123456789ABCDEF' for c in expected_uuid):
            raise RuntimeError('Require the actual candidate executor UUID')
        target = debugger.GetSelectedTarget()
        process = target.GetProcess()
        if target.GetExecutable().GetFilename() != 'qemu-system-x86_64' or process.GetState() != lldb.eStateStopped:
            raise RuntimeError('Attach/stop the owned candidate QEMU first')
        pointer = address(target, 'dg_vram_staged_restore', expected_uuid)
        counter = address(target, 'dg_vram_staged_restores', expected_uuid)
        before = read(process, pointer, 4)
        if struct.unpack('<I', before)[0] not in (0, 1):
            raise RuntimeError('Unexpected candidate flag; no write made')
        if mode != 'status':
            wanted = struct.pack('<I', int(mode == 'on'))
            error = lldb.SBError()
            count = process.WriteMemory(pointer, wanted, error)
            if error.Fail() or count != 4 or read(process, pointer, 4) != wanted:
                process.WriteMemory(pointer, before, error)
                if read(process, pointer, 4) != before:
                    raise RuntimeError('STOP: original comparison flag restoration unconfirmed')
                raise RuntimeError('Comparison write failed; original flag restored')
        result.PutCString(json.dumps({'StagedRestore': struct.unpack('<I', read(process, pointer, 4))[0],
                                     'SuccessfulStagedImports': struct.unpack('<Q', read(process, counter, 8))[0]},
                                    sort_keys=True))
    except RuntimeError as failure:
        result.SetError(str(failure))


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand('command script add -f '+__name__+'.command vim-staged-restore')
