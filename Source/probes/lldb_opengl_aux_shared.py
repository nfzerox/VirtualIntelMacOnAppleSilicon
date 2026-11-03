"""Same-process A/B of our auxiliary coherent-memory route, not an OS patch.

Only a matched GLEngine's source-owned four-byte option is written. Primary
sharing and CPU/GPU exclusion are unchanged. Selection changes on the next
drawable attachment; restore `on` after any comparison. No inferior calls.
"""
import json
import struct
import lldb

UUID = 'B36ADC06D019340C99193D23FABD80F3'


def read(process, address):
    error = lldb.SBError()
    raw = process.ReadMemory(address, 4, error)
    if error.Fail() or len(raw) != 4:
        raise RuntimeError('Cannot read the owned option.')
    value = struct.unpack('<I', raw)[0]
    if value not in (0, 1):
        raise RuntimeError('Unexpected option value; no write made.')
    return value


def command(debugger, text, result, internal_dict):
    mode = text.strip()
    if mode not in ('on', 'off', 'status'):
        result.SetError('Use vim-aux-shared on, off or status.')
        return
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    try:
        if target.GetExecutable().GetFilename() != 'WindowServer' or process.GetState() != lldb.eStateStopped:
            raise RuntimeError('Attach and stop the guest WindowServer first.')
        symbols = target.FindSymbols('vim_aux_shared')
        addresses = []
        for index in range(symbols.GetSize()):
            entry = symbols.GetContextAtIndex(index)
            module = entry.GetModule()
            if module.GetFileSpec().GetFilename() != 'GLEngine':
                continue
            if (module.GetUUIDString() or '').replace('-', '').upper() != UUID:
                raise RuntimeError('Refused an unmatched GLEngine UUID.')
            address = entry.GetSymbol().GetStartAddress()
            if address.GetFileAddress() != 0xc05a4:
                raise RuntimeError('Refused an unmatched source-owned symbol.')
            loaded = address.GetLoadAddress(target)
            if loaded != lldb.LLDB_INVALID_ADDRESS:
                addresses.append(loaded)
        if len(set(addresses)) != 1:
            raise RuntimeError('Expected one matched owned option.')
        address = addresses[0]
        before = read(process, address)
        wanted = before if mode == 'status' else int(mode == 'on')
        if wanted != before:
            error = lldb.SBError()
            written = process.WriteMemory(address, struct.pack('<I', wanted), error)
            if error.Fail() or written != 4 or read(process, address) != wanted:
                process.WriteMemory(address, struct.pack('<I', before), error)
                if read(process, address) != before:
                    raise RuntimeError('STOP: option restoration unconfirmed; do not resume.')
                raise RuntimeError('Write failed; original option restored.')
        result.PutCString(json.dumps({'Address': hex(address), 'Before': before,
            'AuxiliaryShared': bool(read(process, address)), 'UUID': UUID}, sort_keys=True))
    except RuntimeError as failure:
        result.SetError(str(failure))


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand('command script add -f ' + __name__ + '.command vim-aux-shared')
