"""Guarded transient test of scheduled-start frame pacing, audited guests only.

Keep begin_deferred_update_cycle's native closed-loop/VBL transaction batching.
Select the EXISTING scheduled-deadline + refresh-period post-render block in
CGXUpdateDisplay, rather than its completion-relative next-VBL + quarter block.
No new clock, sleep, pixel/damage change, or advertised composition buffers.
This edits six code bytes only in a stopped OWNED-DRIVER guest WindowServer.
No disk, native reference, inferior calls, preferences or host changes. Restore
with vim-scheduled-period off. It is a research test, not installation code.
"""
import json
import lldb

PATCHED = b"\x90" * 6
# File function address, branch-relative offset, original branch and next
# instruction bytes, independently disassembled/decompiled for each binary.
# Lion has the same post-render choice, NOT Mavericks' layout or offsets.
PROFILES = {
    "ED5E3C377E2F38858091DFC9B44B38E8": (0x1bed19, 0x276d,
        b"\x0f\x84\x9c\x00\x00\x00", b"\xf2\x41\x0f\x10\x84\x24\x10\x01\x00\x00"),
    "CB70AC19953238CA85096043AD9B4095": (0x25b34, 0x2306,
        b"\x0f\x84\x8d\x04\x00\x00", b"\xf2\x0f\x10\x80\xd8\x00\x00\x00\xf2\x0f"),
    "576777EA921B3D9498C340A9CF8EBD18": (0x255cc, 0x2306,
        b"\x0f\x84\x8d\x04\x00\x00", b"\xf2\x0f\x10\x80\xd8\x00\x00\x00\xf2\x0f"),
}


def fail(result, message):
    if hasattr(result, 'SetError'):
        result.SetError(message)
    else:
        result.AppendMessage(message)
        result.SetStatus(lldb.eReturnStatusFailed)


def symbol(target, name):
    matches = target.FindSymbols(name)
    found = {}
    for i in range(matches.GetSize()):
        entry = matches.GetContextAtIndex(i)
        module = entry.GetModule()
        if module.GetFileSpec().GetFilename() != "CoreGraphics":
            continue
        if (module.GetUUIDString() or "").replace("-", "").upper() not in PROFILES:
            raise RuntimeError("Refused unmatched CoreGraphics UUID.")
        a = entry.GetSymbol().GetStartAddress()
        loaded = a.GetLoadAddress(target)
        if loaded != lldb.LLDB_INVALID_ADDRESS:
            found[loaded] = a.GetFileAddress()
    if len(found) != 1:
        raise RuntimeError("Expected one matched loaded symbol: " + name)
    return next(iter(found.items()))


def read(process, address, size):
    error = lldb.SBError()
    data = process.ReadMemory(address, size, error)
    if error.Fail() or len(data) != size:
        raise RuntimeError("Cannot read matched code/state.")
    return data


def command(debugger, text, result, internal_dict):
    mode = text.strip()
    if mode not in ("on", "off", "status"):
        fail(result, "Use vim-scheduled-period on, off or status.")
        return
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    if target.GetExecutable().GetFilename() != "WindowServer" or process.GetState() != lldb.eStateStopped:
        fail(result, "Attach and stop the guest WindowServer first.")
        return
    try:
        owned = sum(1 for i in range(target.GetNumModules())
                    if target.GetModuleAtIndex(i).GetFileSpec().GetFilename() == "libVIMPassthroughOpenGL.dylib")
        if owned != 1:
            raise RuntimeError("Refused: not our PassthroughOpenGL guest; never modify the physical reference.")
        start, file_address = symbol(target, "CGXUpdateDisplay")
        profiles=[p for p in PROFILES.values() if p[0] == file_address]
        if len(profiles) != 1:
            raise RuntimeError("Refused unmatched function address.")
        _, offset, original, following = profiles[0]
        address = start + offset
        code = read(process, address, 6)
        if code not in (original, PATCHED) or read(process, address+6, 10) != following:
            raise RuntimeError("Refused unmatched branch/body bytes; no write made.")
        flags = {}
        for name in ("gDeferredUpdates", "gDeferredUpdatesPref", "gOpenLoopScheduler",
                     "gForceDeferredUpdates", "gTripleBufferUpdates"):
            data = read(process, symbol(target, name)[0], 1)
            flags[name] = data[0] if isinstance(data[0], int) else ord(data[0])
        if mode == "on" and flags != dict(gDeferredUpdates=1, gDeferredUpdatesPref=1,
                                           gOpenLoopScheduler=0, gForceDeferredUpdates=0,
                                           gTripleBufferUpdates=0):
            raise RuntimeError("Refused unsafe/changed scheduler policy; no write made.")
        if mode != "status":
            wanted = PATCHED if mode == "on" else original
            error = lldb.SBError()
            written = process.WriteMemory(address, wanted, error)
            if error.Fail() or written != 6 or read(process, address, 6) != wanted:
                # All six bytes are in one page. Still restore/verify the exact
                # original branch if the debugger reports any write failure.
                process.WriteMemory(address, original, error)
                if read(process, address, 6) != original:
                    raise RuntimeError("STOP: original branch restoration unconfirmed; do not resume.")
                raise RuntimeError("Patch write failed; original branch restored.")
            code = wanted
        result.PutCString(json.dumps(dict(Address=hex(address),
            ScheduledStartPeriod=code == PATCHED, Flags=flags), sort_keys=True))
    except RuntimeError as failure:
        fail(result, str(failure))


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand("command script add -f " + __name__ + ".command vim-scheduled-period")
