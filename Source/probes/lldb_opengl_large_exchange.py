"""Guarded same-boot request-bound comparison; no inferior functions.

vim-large-exchange on|off|status EXPECTED_BACKEND_UUID
Only our stopped guest WindowServer, exact single module and binary0/1 flag.
Never attach the host QEMU or physical reference. Detach before workloads.
"""
import json
import lldb
import struct


def command(debugger, text, result, internal_dict):
    parts = text.split()
    if len(parts) != 2 or parts[0] not in ('on', 'off', 'status'):
        result.SetError('Use vim-large-exchange on|off|status EXPECTED_BACKEND_UUID')
        return
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    if target.GetExecutable().GetFilename() != 'WindowServer' or process.GetState() != lldb.eStateStopped:
        result.SetError('Stop the owned-driver guest WindowServer first.')
        return
    modules = [target.GetModuleAtIndex(i) for i in range(target.GetNumModules())
               if target.GetModuleAtIndex(i).GetFileSpec().GetFilename() == 'libVIMPassthroughOpenGL.dylib']
    if len(modules) != 1 or (modules[0].GetUUIDString() or '').replace('-', '').upper() != parts[1].replace('-', '').upper():
        result.SetError('Refused unmatched research backend UUID; no write.')
        return
    matches = modules[0].FindSymbols('dg_large_exchange_override')
    addresses = set()
    for i in range(matches.GetSize()):
        value = matches.GetContextAtIndex(i).GetSymbol().GetStartAddress().GetLoadAddress(target)
        if value != lldb.LLDB_INVALID_ADDRESS:
            addresses.add(value)
    if len(addresses) != 1:
        result.SetError('Missing/ambiguous owned request-bound flag; no write.')
        return
    at = next(iter(addresses))
    error = lldb.SBError()
    data = process.ReadMemory(at, 4, error)
    if error.Fail() or len(data) != 4 or struct.unpack('<I', data)[0] not in (0, 1):
        result.SetError('Invalid owned flag; no write.')
        return
    before = struct.unpack('<I', data)[0]
    after = before if parts[0] == 'status' else int(parts[0] == 'on')
    if after != before:
        wanted = struct.pack('<I', after)
        if process.WriteMemory(at, wanted, error) != 4 or error.Fail() or process.ReadMemory(at, 4, error) != wanted:
            result.SetError('Flag write/readback failed; inspect before resuming.')
            return
    result.PutCString(json.dumps(dict(Before=before, Enabled=bool(after),
        RequestLimit=65536 if after else 4096, ReplyLimit=4096,
        Caution='Still requires negotiated device/provider capability; detach before timing.')))


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand('command script add -f ' + __name__ + '.command vim-large-exchange')
