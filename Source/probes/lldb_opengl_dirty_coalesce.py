"""Owned research QEMU only: exact-build dirty-range A/B, no inferior calls.

Attach, set/status, detach BEFORE timing. The switch does not skip dirtying,
CPU exclusion, validation or GPU completion. Counters cover successful RAM
notifications only, not frame rate, pixel traffic or native GPU time.
"""
import json
import struct
import lldb

UUIDS = ('5DCF30E832EA3F56BBC7AAF8030E945B', '17838DF8C4353A94B31E3078BDD80C75',
         '3C1B6EC221693438BE5A400EBC075599', '10BDAAF683F6312CA4F1CF11B419A708')


def address(target, name):
    matches = target.FindSymbols(name)
    found = set()
    for i in range(matches.GetSize()):
        item = matches.GetContextAtIndex(i)
        module = item.GetModule()
        if module.GetFileSpec() != target.GetExecutable():
            continue
        if (module.GetUUIDString() or '').replace('-', '').upper() not in UUIDS:
            raise RuntimeError('Refused unmatched research QEMU UUID')
        value = item.GetSymbol().GetStartAddress().GetLoadAddress(target)
        if value != lldb.LLDB_INVALID_ADDRESS:
            found.add(value)
    if len(found) != 1:
        raise RuntimeError('Expected one exact research scalar: '+name)
    return next(iter(found))


def read(process, pointer, size):
    error = lldb.SBError()
    value = process.ReadMemory(pointer, size, error)
    if error.Fail() or len(value) != size:
        raise RuntimeError('Cannot read research scalar')
    return value


def command(debugger, text, result, internal_dict):
    try:
        mode = text.strip()
        if mode not in ('on', 'off', 'status'):
            raise RuntimeError('Use vim-dirty-coalesce on, off or status')
        target = debugger.GetSelectedTarget()
        process = target.GetProcess()
        if target.GetExecutable().GetFilename() != 'qemu-system-x86_64' or process.GetState() != lldb.eStateStopped:
            raise RuntimeError('Attach/stop the exact owned research QEMU first')
        pointer = address(target, 'vmsvga_legacy_ram_dirty_coalesce')
        before = read(process, pointer, 1)
        if before not in (b'\0', b'\1'):
            raise RuntimeError('Unexpected comparison value; no write made')
        if mode != 'status':
            wanted = bytes([int(mode == 'on')])
            error = lldb.SBError()
            count = process.WriteMemory(pointer, wanted, error)
            if error.Fail() or count != 1 or read(process, pointer, 1) != wanted:
                process.WriteMemory(pointer, before, error)
                if read(process, pointer, 1) != before:
                    raise RuntimeError('STOP: original comparison value restoration unconfirmed')
                raise RuntimeError('Comparison failed; original value restored')
        output = {'Coalescing': read(process, pointer, 1) == b'\1'}
        for key in ('requests', 'pages', 'segments'):
            output[key] = struct.unpack('<Q', read(process, address(target, 'vmsvga_legacy_ram_dirty_'+key), 8))[0]
        result.PutCString(json.dumps(output, sort_keys=True))
    except RuntimeError as failure:
        result.SetError(str(failure))


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand('command script add -f '+__name__+'.command vim-dirty-coalesce')
