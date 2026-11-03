"""Bounded command counts for ONE primary import->publication segment.

vim-primary-commands arm EXECUTOR_UUID; continue; trigger a real window update;
vim-primary-commands read; detach. This debugger-distorted interval is NOT a
frame-time/FPS measurement. Only opcode/count/geometry metadata is recorded.
"""
import collections
import json
import struct
import time
import lldb

state = {}


def observe(frame, location, internal_dict):
    try:
        process = frame.GetThread().GetProcess()
        target = process.GetTarget()
        # This is a private static function: optimized builds can remove its
        # constant fd argument. Use the actual DWARF locations, not an assumed
        # public SysV register assignment for the source-level signature.
        message = frame.FindVariable('m')
        stream = frame.FindVariable('s')
        if not message.IsValid() or message.GetError().Fail() or not stream.IsValid() or stream.GetError().Fail():
            raise ValueError('Actual compiled argument locations are unavailable')
        pointer = message.GetValueAsUnsigned()
        interpreter = stream.GetValueAsUnsigned()
        error = lldb.SBError()
        data = process.ReadMemory(pointer, 144, error)
        if error.Fail() or len(data) != 144:
            raise ValueError('Cannot read a complete owned wire header')
        op, context, size, flags, *args = struct.unpack('<4I16Q', data)
        if not state.get('key'):
            if op != 0x10038 or args[13] != 0:
                return False
            state['key'] = (interpreter, context, args[10], args[11])
            state['geometry'] = dict(backing=list(args[2:5]), view=list(args[5:10]))
            state['began'] = time.monotonic()
        if time.monotonic() - state['began'] > 15 or state['total'] >= 4096:
            raise ValueError('Bound reached before a complete segment; not a complete-frame count')
        if (interpreter, context) != state['key'][:2]:
            return False
        state['all'][op] += 1
        if flags & 1:
            state['replies'][op] += 1
        state['total'] += 1
        if op == 0x10039 and (args[10], args[11]) == state['key'][2:]:
            state['complete'] = True
            target.FindBreakpointByID(state['breakpoint']).SetEnabled(False)
            return True
        return False
    except Exception as error:
        state['error'] = str(error)
        frame.GetThread().GetProcess().GetTarget().FindBreakpointByID(state['breakpoint']).SetEnabled(False)
        return True


def command(debugger, text, result, internal_dict):
    try:
        parts = text.split()
        if parts == ['read']:
            report = {k: v for k, v in state.items() if k not in ('all', 'replies', 'key', 'began')}
            report['opcodes'] = {hex(k): v for k, v in state.get('all', {}).items()}
            report['reply_opcodes'] = {hex(k): v for k, v in state.get('replies', {}).items()}
            report['Caution'] = 'Import->publish-entry segment, not all work in a frame. Debugger-distorted: counts only, never timing/FPS.'
            result.PutCString(json.dumps(report, sort_keys=True))
            return
        if len(parts) != 2 or parts[0] != 'arm' or state:
            raise ValueError('Arm once with the actual executor UUID, then read after the stop')
        target = debugger.GetSelectedTarget()
        if (target.GetExecutable().GetFilename() != 'qemu-system-x86_64' or
                not target.GetTriple().startswith('x86_64') or
                target.GetProcess().GetState() != lldb.eStateStopped):
            raise ValueError('Require the stopped owned x86_64 QEMU')
        modules = [target.GetModuleAtIndex(i) for i in range(target.GetNumModules())
                   if target.GetModuleAtIndex(i).GetFileSpec().GetFilename() == 'libVIMPassthroughOpenGLHost.dylib']
        if len(modules) != 1 or modules[0].GetUUIDString().replace('-', '').upper() != parts[1].replace('-', '').upper():
            raise ValueError('Unmatched owned executor')
        symbols = modules[0].FindSymbols('DGInterpret')
        addresses = {symbols.GetContextAtIndex(i).GetSymbol().GetStartAddress().GetLoadAddress(target)
                     for i in range(symbols.GetSize())}
        addresses.discard(lldb.LLDB_INVALID_ADDRESS)
        if len(addresses) != 1:
            raise ValueError('Require one actual interpreter entry')
        breakpoint = target.BreakpointCreateByAddress(addresses.pop())
        breakpoint.SetScriptCallbackFunction(__name__ + '.observe')
        state.update(breakpoint=breakpoint.GetID(), all=collections.Counter(),
                     replies=collections.Counter(), total=0, complete=False)
        result.PutCString('READY: bounded command counts, not a pacing measurement')
    except (ValueError, AttributeError) as error:
        result.SetError(str(error))


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand('command script add -f ' + __name__ + '.command vim-primary-commands')
