"""Bounded read-only Mavericks Core Animation destination-texture trace.

For accelerated physical hardware and our guest, not GLRendererFloat.
Offsets are from decompiling the QuartzCore x86_64 UUID below. Refuse other
builds; never call inferior functions. Import after attach, run
vim-ca-destination, continue, exercise the owned window, interrupt/detach.
An observation budget disables every breakpoint, not the desktop process.
"""
import lldb
import struct

UUID = "CFB972D1-FA37-380C-9A47-2A67A84B7442"
breakpoints = []
count = 0


def register(frame, name):
    # Xcode 4.6's LLDB-179 has no SBFrame.FindRegister.
    registers = frame.GetRegisters()
    for i in range(registers.GetSize()):
        group = registers.GetValueAtIndex(i)
        for j in range(group.GetNumChildren()):
            value = group.GetChildAtIndex(j)
            if value.GetName() == name:
                return value.GetValueAsUnsigned()
    raise ValueError("missing register: " + name)


def read(process, address, fmt):
    error = lldb.SBError()
    size = struct.calcsize(fmt)
    data = process.ReadMemory(address, size, error)
    if not error.Success() or len(data) != size:
        raise ValueError("unreadable QuartzCore state")
    return struct.unpack(fmt, data)


def entered(frame, location, _):
    global count
    count += 1
    try:
        process = frame.GetThread().GetProcess()
        obj = register(frame, "rdi")
        # 360: owns generated destination, 364: target, 368: texture name.
        owned, target, texture = read(process, obj + 0x360, "<III")
        cgl = read(process, obj + 0xa0, "<Q")[0]
        extra = ""
        if "ensure_destination" in frame.GetFunctionName():
            bounds = register(frame, "rsi")
            extra = " bounds=" + repr(read(process, bounds, "<4i"))
        print("VIM_CA %d %s object=%x cgl=%x owned=%x target=%x texture=%x%s" %
              (count, frame.GetFunctionName(), obj, cgl, owned, target, texture, extra))
        thread = frame.GetThread()
        print("VIM_CA_CALLERS " + " <- ".join(
            str(thread.GetFrameAtIndex(i).GetFunctionName())
            for i in range(1, min(thread.GetNumFrames(), 7))))
    except Exception as error:
        print("VIM_CA read failed: " + str(error))
    if count >= 96:
        for bp in breakpoints:
            bp.SetEnabled(False)
    return False


def install(debugger, command, result, _):
    global count, breakpoints
    target = debugger.GetSelectedTarget()
    modules = [target.GetModuleAtIndex(i) for i in range(target.GetNumModules())]
    modules = [m for m in modules if m.GetFileSpec().GetFilename() == "QuartzCore"]
    if len(modules) != 1 or modules[0].GetUUIDString().upper() != UUID:
        result.SetError("Matching 13F1911 x86_64 QuartzCore required; no breakpoint installed")
        return
    for bp in breakpoints:
        target.BreakpointDelete(bp.GetID())
    count = 0
    breakpoints = []
    for name in ("CA::OGL::CGLContext::ensure_destination_texture",
                 "CA::OGL::CGLContext::delete_destination_texture"):
        matches = target.FindFunctions(name)
        if matches.GetSize() != 1:
            result.SetError("Expected one native symbol: " + name)
            for bp in breakpoints:
                target.BreakpointDelete(bp.GetID())
            breakpoints = []
            return
        context = matches.GetContextAtIndex(0)
        function = context.GetFunction()
        address = function.GetStartAddress() if function.IsValid() else context.GetSymbol().GetStartAddress()
        bp = target.BreakpointCreateByAddress(address.GetLoadAddress(target))
        breakpoints.append(bp)
        if hasattr(bp, "SetScriptCallbackFunction"):
            bp.SetScriptCallbackFunction(__name__ + ".entered")
        else:
            debugger.HandleCommand('breakpoint command add -s python -o "return ' +
                __name__ + '.entered(frame, bp_loc, internal_dict)" ' + str(bp.GetID()))
        if "delete_destination" in name:
            # collect() calls this for empty destinations every frame. Stop
            # only for a real cached name; the expression only reads memory.
            bp.SetCondition("*(unsigned int*)($rdi+0x368) != 0")
    result.PutCString("Native CA destination trace ready; 96 entries maximum. Continue, then detach promptly.")


def __lldb_init_module(debugger, _):
    debugger.HandleCommand("command script add -f " + __name__ + ".install vim-ca-destination")
    debugger.HandleCommand("command script add -f " + __name__ + ".install_imports vim-native-cgs-imports")


def imported(frame, location, _):
    global count
    count += 1
    try:
        process = frame.GetThread().GetProcess()
        operation = register(frame, "rsi")
        address = register(frame, "rdx")
        words = read(process, address, "<8I" if operation == 0x3e5 else "<5I")
        print("NATIVE_CGS_IMPORT %d op=%x words=%s" %
              (count, operation, ",".join("%x" % value for value in words)))
        thread = frame.GetThread()
        print("NATIVE_CGS_CALLERS " + " <- ".join(
            str(thread.GetFrameAtIndex(i).GetFunctionName())
            for i in range(1, min(thread.GetNumFrames(), 7))))
    except Exception as error:
        print("NATIVE_CGS_IMPORT read failed: " + str(error))
    if count >= 48:
        for bp in breakpoints:
            bp.SetEnabled(False)
    return False


def install_imports(debugger, command, result, _):
    global count, breakpoints
    target = debugger.GetSelectedTarget()
    modules = [target.GetModuleAtIndex(i) for i in range(target.GetNumModules())]
    modules = [m for m in modules if m.GetFileSpec().GetFilename() == "GLEngine"]
    if len(modules) != 1 or modules[0].GetUUIDString().upper() != "30643C97-7EBB-36EE-BEEA-13844EA0F680":
        result.SetError("Matching native 13F1911 GLEngine required; no breakpoint installed")
        return
    matches = target.FindFunctions("gliSetInteger")
    if matches.GetSize() != 1:
        result.SetError("Expected one native gliSetInteger symbol")
        return
    for bp in breakpoints:
        target.BreakpointDelete(bp.GetID())
    count = 0
    context = matches.GetContextAtIndex(0)
    function = context.GetFunction()
    address = function.GetStartAddress() if function.IsValid() else context.GetSymbol().GetStartAddress()
    bp = target.BreakpointCreateByAddress(address.GetLoadAddress(target))
    bp.SetCondition("$rsi == 0x3e5 || $rsi == 0x3e7")
    breakpoints = [bp]
    if hasattr(bp, "SetScriptCallbackFunction"):
        bp.SetScriptCallbackFunction(__name__ + ".imported")
    else:
        debugger.HandleCommand('breakpoint command add -s python -o "return ' +
            __name__ + '.imported(frame, bp_loc, internal_dict)" ' + str(bp.GetID()))
    result.PutCString("Native CGS import trace ready; 48 entries maximum. Continue, then detach promptly.")
