"""Bounded native Mavericks accelerated draw/attachment state, no inferior calls.

Import after attaching, run vim-native-draw-state, continue, then detach
promptly. Command-list continue is explicit for old LLDB-179.
At most three observations per entry point are collected. Matching native
GLEngine and QuartzCore UUIDs are required; this never traces Float.
"""
import lldb
import struct

counts = {}
breakpoints = []


def register(frame, name):
    groups = frame.GetRegisters()
    for i in range(groups.GetSize()):
        group = groups.GetValueAtIndex(i)
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
        raise ValueError("unreadable native state")
    return struct.unpack(fmt, data)


def entered(frame, location, _):
    name = frame.GetFunctionName()
    counts[name] = counts.get(name, 0) + 1
    try:
        process = frame.GetThread().GetProcess()
        args = [register(frame, r) for r in ("rdi", "rsi", "rdx", "rcx", "r8")]
        detail = ""
        if "gliAttachDrawable" in name and args[1] == 0x5a:
            detail = " words=" + repr(read(process, args[2], "<6I"))
        elif "ensure_destination_texture" in name:
            # Exact-build fields established in ensure_destination_texture.
            detail = " bounds=" + repr(read(process, args[1], "<4i"))
            detail += " flags=" + repr(read(process, args[0] + 0x3c0, "<2I"))
            detail += " destination=" + repr(read(process, args[0] + 0x360, "<3I"))
        print("NATIVE_DRAW_STATE %s #%d args=%s%s" %
              (name, counts[name], ",".join("%x" % a for a in args), detail))
        thread = frame.GetThread()
        print("NATIVE_DRAW_CALLERS " + " <- ".join(
            str(thread.GetFrameAtIndex(i).GetFunctionName())
            for i in range(1, min(thread.GetNumFrames(), 6))))
    except Exception as error:
        print("NATIVE_DRAW_STATE read failed: " + str(error))
    if counts[name] >= 3:
        if location is not None:
            location.GetBreakpoint().SetEnabled(False)
        else:
            for bp in breakpoints:
                for i in range(bp.GetNumLocations()):
                    if bp.GetLocationAtIndex(i).GetAddress().GetLoadAddress(process.GetTarget()) == frame.GetPC():
                        bp.SetEnabled(False)
    return False


def command_event():
    process = lldb.debugger.GetSelectedTarget().GetProcess()
    try:
        entered(process.GetSelectedThread().GetFrameAtIndex(0), None, None)
    finally:
        lldb.debugger.HandleCommand("continue")


def install(debugger, command, result, _):
    global counts, breakpoints
    target = debugger.GetSelectedTarget()
    modules = [target.GetModuleAtIndex(i) for i in range(target.GetNumModules())]
    expected = {"GLEngine": "30643C97-7EBB-36EE-BEEA-13844EA0F680",
                "QuartzCore": "CFB972D1-FA37-380C-9A47-2A67A84B7442"}
    for name, uuid in expected.items():
        found = [m for m in modules if m.GetFileSpec().GetFilename() == name]
        if len(found) != 1 or found[0].GetUUIDString().upper() != uuid:
            result.PutCString("REFUSED: matching native 13F1911 " + name + " required; no breakpoints installed")
            return
    for bp in breakpoints:
        target.BreakpointDelete(bp.GetID())
    counts = {}
    breakpoints = []
    names = ("gliAttachDrawableWithOptions", "glColorMask_Exec", "glClear_Exec",
             "CA::OGL::CGLContext::ensure_destination_texture")
    addresses = []
    for name in names:
        matches = target.FindFunctions(name)
        found = []
        module = "QuartzCore" if "CA::" in name else "GLEngine"
        for i in range(matches.GetSize()):
            context = matches.GetContextAtIndex(i)
            function = context.GetFunction()
            address = function.GetStartAddress() if function.IsValid() else context.GetSymbol().GetStartAddress()
            if address.GetModule().GetFileSpec().GetFilename() == module:
                value = address.GetLoadAddress(target)
                if value not in found:
                    found.append(value)
        if not found and module == "QuartzCore":
            # LLDB-179 does not resolve this qualified C++ prefix through
            # FindFunctions. Use the decompiled file entry only after the
            # exact native UUID guard above, never an OS-version guess.
            quartz = [m for m in modules if m.GetFileSpec().GetFilename() == module][0]
            found.append(quartz.ResolveFileAddress(0x1140c6).GetLoadAddress(target))
        if not found or len(found) > 2:
            result.PutCString("REFUSED: unexpected native symbol count for " + name)
            return
        result.PutCString("Native entries for " + name + ": " + repr(found))
        addresses.extend(found)
    for address in addresses:
        bp = target.BreakpointCreateByAddress(address)
        breakpoints.append(bp)
        # LLDB-179 accepts only one -o: repeating it silently keeps only the
        # final command and would discard the bounded event/disable logic.
        debugger.HandleCommand('breakpoint command add -s command -o "script ' +
            __name__ + '.command_event()" ' + str(bp.GetID()))
    result.PutCString("Native draw-state trace ready: three entries/name maximum. Continue, then detach promptly.")


def __lldb_init_module(debugger, _):
    debugger.HandleCommand("command script add -f " + __name__ + ".install vim-native-draw-state")
