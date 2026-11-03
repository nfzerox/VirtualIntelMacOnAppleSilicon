"""Transient same-boot A/B of two EXISTING canonical native readback paths.

Only our matched in-process executor's source-owned sig_atomic_t is written.
No OS binary, guest, entitlement, lock flag, shader, pixel or CPU/GPU ownership
policy is patched. Both paths synchronize real data. Restore with iosurface.
"""
import json
import struct
import lldb

UUID = 'D014D27D4A2337AD98F15F5E7FD36D06'


def read(process, address):
    error = lldb.SBError()
    data = process.ReadMemory(address, 4, error)
    if error.Fail() or len(data) != 4:
        raise RuntimeError('Cannot read the owned executor option.')
    value = struct.unpack('<I', data)[0]
    if value not in (0, 1):
        raise RuntimeError('Unexpected option value; no write made.')
    return value


def command(debugger, text, result, internal_dict):
    mode = text.strip()
    if mode not in ('iosurface', 'readpixels', 'status'):
        result.SetError('Use vim-host-readback iosurface, readpixels or status.')
        return
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    try:
        if target.GetExecutable().GetFilename() != 'qemu-system-x86_64' or process.GetState() != lldb.eStateStopped:
            raise RuntimeError('Attach and stop our host QEMU first.')
        matches = target.FindSymbols('dg_iosurface_readback')
        addresses = []
        for i in range(matches.GetSize()):
            entry = matches.GetContextAtIndex(i)
            module = entry.GetModule()
            if module.GetFileSpec().GetFilename() != 'libVIMPassthroughOpenGLHost.dylib':
                continue
            if (module.GetUUIDString() or '').replace('-', '').upper() != UUID:
                raise RuntimeError('Refused an unmatched executor UUID.')
            address = entry.GetSymbol().GetStartAddress()
            if address.GetFileAddress() != 0x2a888:
                raise RuntimeError('Refused an unmatched source-owned option symbol.')
            loaded = address.GetLoadAddress(target)
            if loaded != lldb.LLDB_INVALID_ADDRESS:
                addresses.append(loaded)
        if len(set(addresses)) != 1:
            raise RuntimeError('Expected one matched owned-executor option.')
        address = addresses[0]
        before = read(process, address)
        wanted = before if mode == 'status' else int(mode == 'iosurface')
        if wanted != before:
            error = lldb.SBError()
            written = process.WriteMemory(address, struct.pack('<I', wanted), error)
            if error.Fail() or written != 4 or read(process, address) != wanted:
                process.WriteMemory(address, struct.pack('<I', before), error)
                if read(process, address) != before:
                    raise RuntimeError('STOP: option restoration unconfirmed; do not resume.')
                raise RuntimeError('Write failed; original option restored.')
        result.PutCString(json.dumps({'Address': hex(address), 'Before': before,
            'IOSurfaceReadback': bool(read(process, address)), 'UUID': UUID}, sort_keys=True))
    except RuntimeError as failure:
        result.SetError(str(failure))


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand('command script add -f ' + __name__ + '.command vim-host-readback')
