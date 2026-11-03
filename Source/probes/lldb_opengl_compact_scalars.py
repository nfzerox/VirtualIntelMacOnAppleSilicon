"""Quiescent owned-client header codec A/B; no inferior calls or guessed heap.

vim-compact-scalars on|off|status EXPECTED_CLIENT_UUID
vim-draw-capture on|off|status EXPECTED_CLIENT_UUID
Changing policy never replays/drops a pending prefix. Negotiation still gates
new containers; disabling merely appends ordinary packets after that prefix.
"""
import json
import lldb
import struct


def policy(debugger, text, result, symbol_name, field, command_name):
    parts = text.split()
    if len(parts) != 2 or parts[0] not in ('on', 'off', 'status'):
        result.SetError('Use ' + command_name + ' on|off|status EXPECTED_CLIENT_UUID')
        return
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    if target.GetExecutable().GetFilename() != 'WindowServer' or process.GetState() != lldb.eStateStopped:
        result.SetError('Stop the owned-driver guest WindowServer first; no write.')
        return
    modules = [target.GetModuleAtIndex(i) for i in range(target.GetNumModules())
               if target.GetModuleAtIndex(i).GetFileSpec().GetFilename() == 'libVIMPassthroughOpenGL.dylib']
    wanted = parts[1].replace('-', '').upper()
    if len(modules) != 1 or (modules[0].GetUUIDString() or '').replace('-', '').upper() != wanted:
        result.SetError('Refused unmatched client UUID; no write.')
        return
    entries = modules[0].FindSymbols(symbol_name)
    addresses = {entries.GetContextAtIndex(i).GetSymbol().GetStartAddress().GetLoadAddress(target)
                 for i in range(entries.GetSize())}
    if len(addresses) != 1 or lldb.LLDB_INVALID_ADDRESS in addresses:
        result.SetError('Missing/ambiguous owned codec policy; no write.')
        return
    at = next(iter(addresses))
    error = lldb.SBError()
    data = process.ReadMemory(at, 4, error)
    if error.Fail() or len(data) != 4 or struct.unpack('<I', data)[0] not in (0, 1):
        result.SetError('Unexpected codec policy; no write.')
        return
    if parts[0] != 'status':
        data = struct.pack('<I', parts[0] == 'on')
        if process.WriteMemory(at, data, error) != 4 or error.Fail() or process.ReadMemory(at, 4, error) != data or error.Fail():
            result.SetError('Policy write/readback failed; inspect before resuming.')
            return
    state = dict(NativeValidationUnchanged=True)
    state[field] = bool(struct.unpack('<I', data)[0])
    if field == 'CompactScalars':
        state['NegotiationStillRequired'] = True
    result.PutCString(json.dumps(state))


def command(debugger, text, result, internal_dict):
    policy(debugger, text, result, 'dg_compact_scalar_override', 'CompactScalars', 'vim-compact-scalars')


def draw_capture_command(debugger, text, result, internal_dict):
    policy(debugger, text, result, 'dg_draw_capture_coalescing', 'DrawCaptureCoalescing', 'vim-draw-capture')


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand('command script add -f ' + __name__ + '.command vim-compact-scalars')
    debugger.HandleCommand('command script add -f ' + __name__ + '.draw_capture_command vim-draw-capture')
