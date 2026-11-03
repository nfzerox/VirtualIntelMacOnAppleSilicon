"""Read-only current research executor selection; no calls or memory writes."""
import json
import lldb

UUID = 'ADA975EF976C3071B0CE3848342E2B3E'


def command(debugger, text, result, internal_dict):
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    try:
        if text.strip() or process.GetState() != lldb.eStateStopped or target.GetExecutable().GetFilename() != 'qemu-system-x86_64':
            raise RuntimeError('Attach/stop the owned research QEMU; no arguments.')
        values = {}
        for name in ('dg_inprocess_inline', 'dg_inline_count'):
            matches = target.FindSymbols(name)
            addresses = set()
            for index in range(matches.GetSize()):
                entry = matches.GetContextAtIndex(index)
                module = entry.GetModule()
                if module.GetFileSpec().GetFilename() != 'libVIMPassthroughOpenGLHost.dylib':
                    continue
                if (module.GetUUIDString() or '').replace('-', '').upper() != UUID:
                    raise RuntimeError('Refused unmatched executor UUID.')
                address = entry.GetSymbol().GetStartAddress().GetLoadAddress(target)
                if address != lldb.LLDB_INVALID_ADDRESS:
                    addresses.add(address)
            if len(addresses) != 1:
                raise RuntimeError('Expected exactly one owned scalar symbol: ' + name)
            error = lldb.SBError()
            values[name] = process.ReadUnsignedFromMemory(addresses.pop(), 4, error)
            if error.Fail():
                raise RuntimeError('Cannot read owned scalar.')
        if values['dg_inprocess_inline'] not in (0, 1) or values['dg_inline_count'] > 128:
            raise RuntimeError('Unexpected owned scalar values.')
        result.PutCString(json.dumps(values, sort_keys=True))
    except RuntimeError as failure:
        result.SetError(str(failure))


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand('command script add -f ' + __name__ + '.command vim-inline-status')
