"""Bounded read-only Mavericks move/copy trace; works with LLDB-179.

IOFB signatures/region layout come from the 10.9 SDK, and the caller was
decompiled in matching 13F1911 CoreGraphics. No inferior calls or writes.
The stripped GA's old LLDB symbol labels are not trusted: discover its
actual callback through the UUID-guarded IOKit record instead.
Attach, import, vim-move-trace, continue; detach promptly after the fixture.
Six entries and corresponding returns per function maximum.
"""
import lldb
import struct

counts = {}
entries = {}
returns = {}
breakpoints = []


def register_value(frame, name):
    groups = frame.GetRegisters()
    for i in range(groups.GetSize()):
        group = groups.GetValueAtIndex(i)
        for j in range(group.GetNumChildren()):
            value = group.GetChildAtIndex(j)
            if value.GetName() == name:
                return value
    raise ValueError("missing register " + name)


def reg(frame, name):
    return register_value(frame,name).GetValueAsUnsigned()


def read(process, address, fmt):
    error = lldb.SBError()
    data = process.ReadMemory(address, struct.calcsize(fmt), error)
    if not error.Success() or len(data) != struct.calcsize(fmt):
        raise ValueError("unreadable move record")
    return struct.unpack(fmt, data)


def region(process, address):
    if not address:
        return "none"
    # SDK IOAccelDeviceRegion is uint32 count, then signed INT16 bounds.
    header = read(process, address, "<I4h")
    n = min(header[0], 4)
    return repr(header) + " rects=" + repr(read(process, address + 12, "<%dh" % (n * 4)))


def drawable(process, address):
    # Exact own-adapter layout, unchanged since the prior CGS texture trace.
    surface = read(process, address + 8, "<I")[0]
    detail = "surface=%x size=%s flags,origin=%s" % (surface,
        repr(read(process, address + 20, "<2i")), repr(read(process, address + 104, "<6i")))
    clip = read(process, address + 128, "<Q")[0]
    if clip:
        header=read(process, clip, "<2I2i")
        detail += " clip=" + repr(header)
        detail += " rects=" + repr(read(process, clip + 16, "<%dh" % (min(header[1],4)*4)))
    return surface, detail


def doubles(frame):
    values=[]
    for i in range(6):
        value=register_value(frame,"xmm%d" % i);error=lldb.SBError()
        values.append(value.GetData().GetDouble(error,0))
        if not error.Success():raise ValueError("unreadable SSE register")
    return repr(values)


def callback(bp):
    lldb.debugger.HandleCommand('breakpoint command add -s command -o "script ' +
                               __name__ + '.event()" ' + str(bp.GetID()))


def event():
    target = lldb.debugger.GetSelectedTarget()
    process = target.GetProcess()
    frame = process.GetSelectedThread().GetFrameAtIndex(0)
    try:
        pc = frame.GetPC()
        key = (pc, frame.GetThread().GetThreadID())
        if key in returns:
            print("MOVE_RETURN %s rc=%x" % (returns.pop(key), reg(frame, "rax") & 0xffffffff))
        else:
            name, bp = entries[pc]
            args = [reg(frame, r) for r in ("rdi", "rsi", "rdx", "rcx", "r8", "r9")]
            detail = ""
            if name.startswith("VIM"):
                d = args[1] if name == "VIMImportAcceleratorBacking" else read(process, args[0] + 8096, "<Q")[0]
                if not d:
                    return
                surface, detail = drawable(process, d)
                if surface != 1:
                    return
            counts[name] = counts.get(name, 0) + 1
            # Bound even a formatter/old-debugger API failure, not only the
            # successful print path. A broken probe must not keep stopping WS.
            if counts[name] >= 6:
                bp.SetEnabled(False)
            if name == "IOFBBlitVRAMCopy":
                detail = " stack=" + repr(read(process, reg(frame, "rsp") + 8, "<2Q"))
            elif name == "IOFBBlitSurfaceCopy":
                detail = " region=" + region(process, args[3])
                # Matching 13F1911 IOKit 10932113... disassembly loads the
                # selected plugin callback from this private blitter record.
                module=frame.GetModule()
                if module.GetUUIDString().upper()=="10932113-9F7E-38A0-A158-A019A555CAC3":
                    entry=read(process,args[0]+0xa0,"<Q")[0]
                    address=target.ResolveLoadAddress(entry)
                    detail += " callback=%s:%x:%s" % (address.GetModule().GetFileSpec().GetFilename(),
                        address.GetFileAddress(),str(address.GetSymbol().GetName()))
                    if entry not in entries:
                        cb=target.BreakpointCreateByAddress(entry)
                        entries[entry]=("IOFBActualCopy",cb);breakpoints.append(cb);callback(cb)
            elif name == "IOFBActualCopy":
                detail = " delta=" + repr(read(process,args[4]+88,"<2i"))
                detail += " region=" + region(process,read(process,args[4]+96,"<Q")[0])
            elif name in ("VIMDispatch_ortho", "glOrtho_Exec"):
                detail += " projection=" + doubles(frame)
            elif name == "gliAttachDrawableWithOptions" and args[1] == 0x5a:
                detail += " attach=" + repr(read(process,args[2],"<6I"))
            print("MOVE_ENTRY %s #%d args=%s%s" %
                  (name, counts[name], ",".join("%x" % a for a in args), detail))
            print("MOVE_CALLERS " + " <- ".join(str(frame.GetThread().GetFrameAtIndex(i).GetFunctionName())
                  for i in range(1, min(frame.GetThread().GetNumFrames(), 7))))
            if name in ("IOFBBlitVRAMCopy", "IOFBBlitSurfaceCopy", "IOFBActualCopy", "VIMImportAcceleratorBacking"):
                ret = read(process, reg(frame, "rsp"), "<Q")[0]
                rbp = target.BreakpointCreateByAddress(ret)
                rbp.SetThreadID(frame.GetThread().GetThreadID());rbp.SetOneShot(True);callback(rbp)
                breakpoints.append(rbp)
                returns[(ret, frame.GetThread().GetThreadID())] = name
            if counts[name] >= 6:
                bp.SetEnabled(False)
    except Exception as error:
        print("MOVE_TRACE unavailable: " + str(error))
    finally:
        lldb.debugger.HandleCommand("continue")


def install(debugger, command, result, _):
    global counts, entries, returns, breakpoints
    target = debugger.GetSelectedTarget()
    for bp in breakpoints:
        target.BreakpointDelete(bp.GetID())
    counts={};entries={};returns={};breakpoints=[]
    # Only these own-adapter builds have the exact layout used above. This
    # probe must be requalified when that layout or the compiled engine changes.
    native = command.strip() in ("native", "native-copy")
    qualified=("EA5D52B3-DEA5-30D0-910A-CA4895BB6EEE",
               "D24218AF-B20B-3E3C-A78D-5FED9FF3FA00")
    if native:qualified=("30643C97-7EBB-36EE-BEEA-13844EA0F680",)
    engines=[target.GetModuleAtIndex(i) for i in range(target.GetNumModules())
             if target.GetModuleAtIndex(i).GetFileSpec().GetFilename()=="GLEngine"]
    if len(engines)!=1 or engines[0].GetUUIDString().upper() not in qualified:
        result.PutCString("REFUSED: move trace requires the qualified native/own-adapter build")
        return
    names = ("IOFBBlitVRAMCopy", "IOFBBlitSurfaceCopy",
             "VIMImportAcceleratorBacking", "VIMPresentDrawable", "VIMDispatch_viewport",
             "VIMDispatch_ortho", "VIMDispatch_scissor")
    if native:names=("gliAttachDrawableWithOptions", "glViewport_Exec", "glOrtho_Exec", "glScissor_Exec")
    if command.strip()=="native-copy":names=("IOFBBlitVRAMCopy", "IOFBBlitSurfaceCopy")
    for name in names:
        matches = target.FindFunctions(name)
        candidates = []
        for i in range(matches.GetSize()):
            context = matches.GetContextAtIndex(i);function = context.GetFunction()
            address = function.GetStartAddress() if function.IsValid() else context.GetSymbol().GetStartAddress()
            module = address.GetModule().GetFileSpec().GetFilename()
            expected = "IOKit" if name.startswith("IOFB") else "GLEngine"
            if module == expected:
                value = address.GetLoadAddress(target)
                if value not in candidates:
                    candidates.append(value)
        if len(candidates) != 1:
            result.PutCString("Skipping %s: %d exact module entries" % (name, len(candidates)))
            continue
        bp = target.BreakpointCreateByAddress(candidates[0]);entries[candidates[0]] = (name, bp);callback(bp)
        breakpoints.append(bp)
        result.PutCString("Move trace entry: %s at %x" % (name, candidates[0]))
    result.PutCString("Continue now; detach promptly after the bounded moving-window fixture.")


def __lldb_init_module(debugger, _):
    debugger.HandleCommand("command script add -f " + __name__ + ".install vim-move-trace")
