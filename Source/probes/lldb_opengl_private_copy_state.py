"""Same-boot owned executor A/B; no inferior calls or host-library changes."""
import json
import struct
import lldb


def set_flag(debugger, text, result, internal_dict, name, label):
    try:
        mode, wanted = text.split()
        if mode not in ('status', 'on', 'off'):
            raise ValueError('Use vim-private-copy-state status|on|off EXECUTOR_UUID')
        target = debugger.GetSelectedTarget()
        process = target.GetProcess()
        if (target.GetExecutable().GetFilename() != 'qemu-system-x86_64' or
                process.GetState() != lldb.eStateStopped):
            raise ValueError('Stop the owned diagnostic QEMU first')
        modules = [target.GetModuleAtIndex(i) for i in range(target.GetNumModules())
                   if target.GetModuleAtIndex(i).GetFileSpec().GetFilename() ==
                   'libVIMPassthroughOpenGLHost.dylib']
        if len(modules) != 1 or modules[0].GetUUIDString().replace('-', '').upper() != wanted.replace('-', '').upper():
            raise ValueError('Unmatched executor; no write')
        symbols = modules[0].FindSymbols(name)
        locations = {symbols.GetContextAtIndex(i).GetSymbol().GetStartAddress().GetLoadAddress(target)
                     for i in range(symbols.GetSize())}
        locations.discard(lldb.LLDB_INVALID_ADDRESS)
        if len(locations) != 1:
            raise ValueError('Missing/ambiguous comparison scalar')
        address = locations.pop()
        error = lldb.SBError()
        value = process.ReadUnsignedFromMemory(address, 4, error)
        if error.Fail() or value not in (0, 1):
            raise ValueError('Unexpected comparison state; no write')
        if mode != 'status':
            value = int(mode == 'on')
            if process.WriteMemory(address, struct.pack('<I', value), error) != 4 or error.Fail():
                raise ValueError('Comparison write failed')
            actual = process.ReadUnsignedFromMemory(address, 4, error)
            if error.Fail() or actual != value:
                raise ValueError('Comparison readback failed; inspect before resuming')
        result.PutCString(json.dumps({label: bool(value),
                                     'ActualCompletionRequired': True}))
    except (ValueError, AttributeError) as error:
        result.SetError(str(error))

def command(debugger, text, result, internal_dict):
    set_flag(debugger, text, result, internal_dict,
             'dg_vram_private_copy_state', 'PrivateCopyStateReuse')


def error_replies(debugger, text, result, internal_dict):
    set_flag(debugger, text, result, internal_dict,
             'dg_error_empty_replies', 'NativeEmptyErrorReplies')


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand('command script add -f ' + __name__ + '.command vim-private-copy-state')
    debugger.HandleCommand('command script add -f ' + __name__ + '.error_replies vim-error-empty-replies')
