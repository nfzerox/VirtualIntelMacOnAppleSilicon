"""Bounded read-only CGS ABI trace for our own Mavericks probe (LLDB/Python 2).

Run via: command script import /path/lldb_opengl_cgs.py; vim-trace-cgs; run
Does not attach to or modify WindowServer or any system library.
On LLDB-179, manually continue scripted stops; callback-return continuation
does not take effect there. The helper remains bounded to six entries/name.
"""
import lldb
import struct

pending = {}
counts = {}


def callback(debugger, bp, function):
    # LLDB-179 (Xcode 4.6) predates SBBreakpoint.SetScriptCallbackFunction.
    debugger.HandleCommand('breakpoint command add -s python -o "return ' +
                           __name__ + '.' + function + '(frame, bp_loc, internal_dict)" ' +
                           str(bp.GetID()))


def reg(frame, name):
    registers = frame.GetRegisters()
    for i in range(registers.GetSize()):
        group = registers.GetValueAtIndex(i)
        for j in range(group.GetNumChildren()):
            value = group.GetChildAtIndex(j)
            if value.GetName() == name:
                return value.GetValueAsUnsigned()
    raise RuntimeError('Missing register ' + name)


def memory(process, address, size):
    error = lldb.SBError()
    data = process.ReadMemory(address, size, error) if address else None
    return data if error.Success() else None


def returned(frame, bp_loc, internal_dict):
    record = pending.pop(bp_loc.GetBreakpoint().GetID(), None)
    if not record:
        return False
    name, args = record
    out = {'return': name, 'rax': reg(frame, 'rax')}
    if name == 'CGSLockWindowBits':
        for label, address, fmt in [('bounds', args[2], '4d'),
                                    ('depth', args[3], 'i'),
                                    ('plane0', args[4], 'Q'),
                                    ('stride0', args[5], 'i')]:
            data = memory(frame.GetThread().GetProcess(), address, struct.calcsize(fmt))
            if data:
                out[label] = struct.unpack(fmt, data)
    print('VIM_CGS ' + repr(out))
    return False


def entered(frame, bp_loc, internal_dict):
    name = frame.GetFunctionName()
    name = name.split('`')[-1].lstrip('_')
    count = counts.get(name, 0) + 1
    counts[name] = count
    if count > 6:
        bp_loc.GetBreakpoint().SetEnabled(False)
        return False
    args = [reg(frame, r) for r in ('rdi', 'rsi', 'rdx', 'rcx', 'r8', 'r9')]
    thread = frame.GetThread()
    callers = [str(thread.GetFrameAtIndex(i).GetFunctionName())
               for i in range(min(5, thread.GetNumFrames()))]
    stack = memory(thread.GetProcess(), reg(frame, 'rsp'), 40)
    out = {'entry': name, 'args': args, 'stack': struct.unpack('5Q', stack) if stack else (),
           'callers': callers}
    print('VIM_CGS ' + repr(out))
    if stack and name == 'CGSLockWindowBits':
        address = struct.unpack('Q', stack[:8])[0]
        bp = thread.GetProcess().GetTarget().BreakpointCreateByAddress(address)
        bp.SetOneShot(True)
        bp.SetThreadID(thread.GetThreadID())
        callback(lldb.debugger, bp, 'returned')
        pending[bp.GetID()] = (name, args)
    return False


def start(debugger, command, result, internal_dict):
    target = debugger.GetSelectedTarget()
    for name in ('CGSNewWindowWithOpaqueShape', 'CGSBindSurface', 'CGSLockWindowBits',
                 'CGSUnlockWindowBits', 'CGSFlushSurfaceWithOptions', 'CGSFlushSurface',
                 'CGSGetSurfaceBounds', 'CGSSetWindowOpacity'):
        bp = target.BreakpointCreateByName(name)
        callback(debugger, bp, 'entered')
    result.PutCString('Bounded native CGS trace enabled (six calls per function).')


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand('command script add -f ' + __name__ + '.start vim-trace-cgs')
