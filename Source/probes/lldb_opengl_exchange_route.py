"""Exact-client, same-boot call-route comparison with identical request bounds.

vim-exchange-route trap-small|method-small|trap-large|status EXPECTED_CLIENT_UUID
Owned stopped guest WindowServer only; no inferior calls or OS/disk changes.
Both flags are restored on a failed write. Detach AND quit before workloads.
"""
import json
import lldb
import struct


def command(debugger, text, result, internal_dict):
    parts = text.split()
    modes = {'trap-small': (1, 0), 'method-small': (0, 0), 'trap-large': (1, 1)}
    if len(parts) != 2 or parts[0] not in tuple(modes) + ('status',):
        result.SetError('Use vim-exchange-route trap-small|method-small|trap-large|status EXPECTED_CLIENT_UUID')
        return
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    if target.GetExecutable().GetFilename() != 'WindowServer' or process.GetState() != lldb.eStateStopped:
        result.SetError('Stop the owned-driver guest WindowServer first.')
        return
    modules = [target.GetModuleAtIndex(i) for i in range(target.GetNumModules())
               if target.GetModuleAtIndex(i).GetFileSpec().GetFilename() == 'libVIMPassthroughOpenGL.dylib']
    if len(modules) != 1 or (modules[0].GetUUIDString() or '').replace('-', '').upper() != parts[1].replace('-', '').upper():
        result.SetError('Refused unmatched owned-client UUID; no write.')
        return
    addresses, before = [], []
    error = lldb.SBError()
    try:
        for name in ('dg_exchange_trap_override', 'dg_large_exchange_override'):
            matches = modules[0].FindSymbols(name)
            values = set()
            for i in range(matches.GetSize()):
                address = matches.GetContextAtIndex(i).GetSymbol().GetStartAddress().GetLoadAddress(target)
                if address != lldb.LLDB_INVALID_ADDRESS:
                    values.add(address)
            if len(values) != 1:
                raise RuntimeError('Missing/ambiguous owned route flag: ' + name)
            at = next(iter(values))
            data = process.ReadMemory(at, 4, error)
            if error.Fail() or len(data) != 4 or struct.unpack('<I', data)[0] not in (0, 1):
                raise RuntimeError('Invalid owned route flag; no write.')
            addresses.append(at)
            before.append(struct.unpack('<I', data)[0])
        after = before if parts[0] == 'status' else modes[parts[0]]
        try:
            for at, old, value in zip(addresses, before, after):
                if old == value:
                    continue
                data = struct.pack('<I', value)
                if process.WriteMemory(at, data, error) != 4 or error.Fail() or process.ReadMemory(at, 4, error) != data:
                    raise RuntimeError('Route flag write/readback failed.')
        except RuntimeError:
            restored = True
            for at, value in zip(addresses, before):
                data = struct.pack('<I', value)
                restored = (process.WriteMemory(at, data, error) == 4 and error.Success() and
                            process.ReadMemory(at, 4, error) == data) and restored
            raise RuntimeError('Route change failed; original flags restored.' if restored else
                               'STOP: original flag restoration unconfirmed; inspect before resuming.')
        result.PutCString(json.dumps(dict(Before=before, Trap=bool(after[0]), Large=bool(after[1]),
            RequestLimit=65536 if all(after) else 4096,
            Caution='Negotiated same ordered exchange; detach AND quit before measuring.')))
    except RuntimeError as failure:
        result.SetError(str(failure))


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand('command script add -f ' + __name__ + '.command vim-exchange-route')
