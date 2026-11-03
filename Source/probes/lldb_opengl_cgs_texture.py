"""Bounded read-only entry tracing for our x86_64 Mavericks GLI adapter.

Offsets below are from clang -fdump-record-layouts of this repository's
VIMContext/VIMCGSDrawable, not guessed private Apple layouts. Requalify them
if those structures change. No inferior expression/function calls are used.
Attach, import this module, run `vim-cgs-textures`, then continue. Interrupt
and detach promptly after the owned moving-window probe; never leave the
desktop stopped. At most 24 entry observations are collected.
"""
import lldb
import struct

_count = 0


def memory(process, address, size):
    error = lldb.SBError()
    data = process.ReadMemory(address, size, error)
    if not error.Success() or len(data) != size:
        raise ValueError("unreadable adapter memory")
    return data


def entered(frame, location, _):
    global _count
    _count += 1
    try:
        process = frame.GetThread().GetProcess()
        context = frame.FindRegister("rdi").GetValueAsUnsigned()
        operation = frame.FindRegister("rsi").GetValueAsUnsigned()
        args = frame.FindRegister("rdx").GetValueAsUnsigned()
        # Native 3e7 supplies five words, unlike the eight-word 3e5 record.
        words = 8 if operation == 0x3e5 else 5
        record = struct.unpack("<%dI" % words, memory(process, args, words * 4))
        backend, pixel_format = struct.unpack("<2Q", memory(process, context + 7816, 16))
        connection = struct.unpack("<Q", memory(process, backend, 8))[0]
        capabilities = struct.unpack("<Q", memory(process, connection + 16, 8))[0]
        double_buffer = struct.unpack("<i", memory(process, pixel_format + 16, 4))[0]
        print("VIM FORMAT double_buffer=%d negotiated_capabilities=%x" % (double_buffer, capabilities))
        drawable = struct.unpack("<Q", memory(process, context + 8096, 8))[0]
        if drawable:
            raw = memory(process, drawable, 128)
            fields = struct.unpack_from("<6i", raw, 104)
            dimensions = struct.unpack_from("<2i", raw, 20)
            surface = struct.unpack_from("<I", raw, 8)[0]
            layout = struct.unpack_from("<6I", raw, 80)
            print("VIM ENTRY %d op=%x record=%s source=%x size=%s "
                  "accel,unflipped,native,view,x,y=%s layout=%s" %
                  (_count, operation, ",".join("%x" % x for x in record),
                   surface, dimensions, fields, layout))
        else:
            print("VIM ENTRY %d op=%x record=%s no drawable" %
                  (_count, operation, record))
        callers = []
        thread = frame.GetThread()
        for i in range(1, min(thread.GetNumFrames(), 9)):
            caller = thread.GetFrameAtIndex(i)
            callers.append("%s@%x" % (caller.GetFunctionName() or "?", caller.GetPC()))
        print("VIM CALLERS " + " <- ".join(callers))
    except Exception as error:
        print("VIM ENTRY read failed: %s" % error)
    if _count >= 24:
        location.GetBreakpoint().SetEnabled(False)
    return False


def install(debugger, command, result, _):
    global _count
    _count = 0
    target = debugger.GetSelectedTarget()
    matches = target.FindFunctions("VIMTextureFromAcceleratorSurface")
    if matches.GetSize() != 1:
        result.SetError("Expected one own-adapter function; no breakpoint installed")
        return
    symbol = matches.GetContextAtIndex(0)
    function = symbol.GetFunction()
    address = function.GetStartAddress() if function.IsValid() else symbol.GetSymbol().GetStartAddress()
    breakpoint = target.BreakpointCreateByAddress(address.GetLoadAddress(target))
    breakpoint.SetScriptCallbackFunction(__name__ + ".entered")
    result.PutCString("Bounded own-adapter entry trace installed: breakpoint %d" % breakpoint.GetID())


def __lldb_init_module(debugger, _):
    debugger.HandleCommand("command script add -f " + __name__ + ".install vim-cgs-textures")
