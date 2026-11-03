"""Read-only, bounded native accelerated GLI 300 -> driver dispatch trace.

The context/dispatch offsets are from matching 13F1911 GLEngine disassembly.
Use with our owned passthrough-opengl-private-integer executable, not a software GLD.
Stop at VIMPrivateIntegerInvoke, import, vim-private-integer, continue.
No inferior calls or writes.
"""
import lldb
import struct

breakpoints = []
returns = {}
counts = {}


def reg(frame, name):
    for group in frame.GetRegisters():
        for value in group:
            if value.GetName() == name:
                return value.GetValueAsUnsigned()
    raise ValueError("missing register " + name)


def read(process, address, fmt):
    error = lldb.SBError()
    size = struct.calcsize(fmt)
    data = process.ReadMemory(address, size, error)
    if not error.Success() or len(data) != size:
        raise ValueError("unreadable native integer state")
    return struct.unpack(fmt, data)


def callback(debugger, bp):
    debugger.HandleCommand('breakpoint command add -s command -o "script ' +
        __name__ + '.event()" ' + str(bp.GetID()))


def event():
    debugger = lldb.debugger
    target = debugger.GetSelectedTarget();process = target.GetProcess()
    frame = process.GetSelectedThread().GetFrameAtIndex(0)
    try:
        key = (frame.GetPC(), frame.GetThread().GetThreadID())
        if key in returns:
            print("NATIVE_INTEGER_RETURN %s rc=%x" %
                  (returns.pop(key), reg(frame, "rax") & 0xffffffff))
            return
        command = reg(frame, "rsi")
        if command != 300:
            return
        module = frame.GetModule().GetFileSpec().GetFilename()
        counts[module] = counts.get(module, 0) + 1
        if counts[module] > 2:
            return
        print("NATIVE_INTEGER_ENTRY module=%s uuid=%s file=%x context=%x command=%x" %
              (module, frame.GetModule().GetUUIDString(),
               frame.GetPCAddress().GetFileAddress(), reg(frame, "rdi"), command))
        if module == "GLEngine":
            # Abort before any native layout read if this is a different build.
            if frame.GetModule().GetUUIDString().upper() != "30643C97-7EBB-36EE-BEEA-13844EA0F680":
                print("REFUSED native GLEngine UUID mismatch");return
            context = reg(frame, "rdi")
            dispatch = read(process, context + 0xd550, "<Q")[0]
            entry = read(process, dispatch + 0xc0, "<Q")[0]
            address = target.ResolveLoadAddress(entry)
            print("NATIVE_INTEGER_DRIVER module=%s uuid=%s file=%x" %
                  (address.GetModule().GetFileSpec().GetFilename(),
                   address.GetModule().GetUUIDString(), address.GetFileAddress()))
            bp = target.BreakpointCreateByAddress(entry);breakpoints.append(bp)
            callback(debugger, bp)
        elif module == "libGPUSupportMercury.dylib":
            if frame.GetModule().GetUUIDString().upper() != "1E23BD90-2A15-30E9-9CDF-255D64D404D0":
                print("REFUSED native GPUSupport UUID mismatch");return
            dispatch = read(process, reg(frame, "rdi") + 8, "<Q")[0]
            entry = read(process, dispatch + 0x1e0, "<Q")[0]
            address = target.ResolveLoadAddress(entry)
            print("NATIVE_INTEGER_HARDWARE module=%s uuid=%s file=%x" %
                  (address.GetModule().GetFileSpec().GetFilename(),
                   address.GetModule().GetUUIDString(), address.GetFileAddress()))
            bp = target.BreakpointCreateByAddress(entry);breakpoints.append(bp)
            callback(debugger, bp)
        elif module == "GeForceGLDriver":
            if frame.GetModule().GetUUIDString().upper() != "B1F1C094-43E0-3A3D-9DEB-7A7C6C36A340":
                print("REFUSED native NVIDIA UUID mismatch");return
            machine = read(process, reg(frame, "rdi") + 0x1a28, "<Q")[0]
            entry = read(process, machine + 0x22938, "<Q")[0]
            address = target.ResolveLoadAddress(entry)
            print("NATIVE_INTEGER_OPERATION module=%s uuid=%s file=%x" %
                  (address.GetModule().GetFileSpec().GetFilename(),
                   address.GetModule().GetUUIDString(), address.GetFileAddress()))
        ret = read(process, reg(frame, "rsp"), "<Q")[0]
        bp = target.BreakpointCreateByAddress(ret)
        bp.SetThreadID(frame.GetThread().GetThreadID());bp.SetOneShot(True)
        breakpoints.append(bp);callback(debugger, bp)
        returns[(ret, frame.GetThread().GetThreadID())] = module
    except Exception as error:
        print("NATIVE_INTEGER unavailable: " + str(error))
    finally:
        debugger.HandleCommand("continue")


def install(debugger, command, result, _):
    global breakpoints, returns, counts
    target = debugger.GetSelectedTarget()
    for bp in breakpoints:
        target.BreakpointDelete(bp.GetID())
    breakpoints=[];returns={};counts={}
    matches = target.FindFunctions("gliSetInteger")
    entries = []
    for i in range(matches.GetSize()):
        context = matches.GetContextAtIndex(i);function = context.GetFunction()
        address = function.GetStartAddress() if function.IsValid() else context.GetSymbol().GetStartAddress()
        module = address.GetModule()
        if module.GetFileSpec().GetFilename() == "GLEngine":
            if module.GetUUIDString().upper() != "30643C97-7EBB-36EE-BEEA-13844EA0F680":
                result.PutCString("REFUSED native GLEngine UUID mismatch");return
            value = address.GetLoadAddress(target)
            if value not in entries:entries.append(value)
    if len(entries) != 1:
        result.PutCString("REFUSED: stop at VIMPrivateIntegerInvoke after GLEngine is loaded");return
    # Exact symbol entry, not a source-line breakpoint after the prologue.
    bp = target.BreakpointCreateByAddress(entries[0])
    breakpoints.append(bp);callback(debugger, bp)
    result.PutCString("Native GLI 300 trace ready; continue owned fixture, not WindowServer.")


def __lldb_init_module(debugger, _):
    debugger.HandleCommand("command script add -f " + __name__ + ".install vim-private-integer")
