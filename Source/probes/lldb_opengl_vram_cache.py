"""Stopped-own-QEMU backing-registration A/B; no inferior calls/OS writes.

Match the exact research library before changing its source-owned option.
Restore on after the comparison. Existing copies, CPU ownership and fences
are unchanged; off selects the original discard-on-backing-switch route.
"""
import json
import struct
import lldb

UUID = 'B64FC6E524C13A03897B9765A07ECFFD'


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
        result.SetError('Use vim-vram-cache on, off or status.')
        return
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    try:
        if target.GetExecutable().GetFilename() != 'qemu-system-x86_64' or process.GetState() != lldb.eStateStopped:
            raise RuntimeError('Attach and stop this research QEMU first.')
        symbols = target.FindSymbols('dg_vram_backing_reuse')
        addresses = []
        for index in range(symbols.GetSize()):
            entry = symbols.GetContextAtIndex(index)
            module = entry.GetModule()
            if module.GetFileSpec().GetFilename() != 'libVIMPassthroughOpenGLHost.dylib':
                continue
            if (module.GetUUIDString() or '').replace('-', '').upper() != UUID:
                raise RuntimeError('Refused an unmatched research library UUID.')
            address = entry.GetSymbol().GetStartAddress()
            if address.GetFileAddress() != 0x2a854:
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
            'BackingReuse': bool(read(process, address)), 'UUID': UUID}, sort_keys=True))
    except RuntimeError as failure:
        result.SetError(str(failure))


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand('command script add -f ' + __name__ + '.command vim-vram-cache')
