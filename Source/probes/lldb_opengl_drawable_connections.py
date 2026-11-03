"""Same-boot bounded connection-reuse comparison in our stopped Mavericks WS.

Only the compiled research engine's int toggle can be written. Read its three
aggregate counters without calling inferior functions or changing GL state.
This is a diagnostic, not guest installation or physical-Mac code.
"""
import json
import struct
import lldb

UUID = "5F7D2C71D4403C1BB4A234A05530DD14"


def address(target, name):
    matches = target.FindSymbols(name)
    found = set()
    for index in range(matches.GetSize()):
        entry = matches.GetContextAtIndex(index)
        module = entry.GetModule()
        if module.GetFileSpec().GetFilename() != "GLEngine":
            continue
        if (module.GetUUIDString() or "").replace("-", "").upper() != UUID:
            raise RuntimeError("Refused unmatched research GLEngine UUID.")
        loaded = entry.GetSymbol().GetStartAddress().GetLoadAddress(target)
        if loaded != lldb.LLDB_INVALID_ADDRESS:
            found.add(loaded)
    if len(found) != 1:
        raise RuntimeError("Expected one matched symbol: " + name)
    return next(iter(found))


def read(process, location, size):
    error = lldb.SBError()
    data = process.ReadMemory(location, size, error)
    if error.Fail() or len(data) != size:
        raise RuntimeError("Cannot read matched research state.")
    return data


def command(debugger, text, result, internal_dict):
    mode = text.strip()
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    if mode not in ("on", "off", "status"):
        result.SetError("Use vim-drawable-connections on, off or status.")
        return
    try:
        if target.GetExecutable().GetFilename() != "WindowServer" or process.GetState() != lldb.eStateStopped:
            raise RuntimeError("Attach and stop the research guest WindowServer first.")
        owned = sum(target.GetModuleAtIndex(i).GetFileSpec().GetFilename() == "libVIMPassthroughOpenGL.dylib"
                    for i in range(target.GetNumModules()))
        if owned != 1:
            raise RuntimeError("Refused non-research renderer; never alter the physical reference.")
        toggle = address(target, "vim_reuse_drawable_connection")
        counters = address(target, "vim_drawable_connection_stats")
        enabled = struct.unpack("<i", read(process, toggle, 4))[0]
        if enabled not in (0, 1):
            raise RuntimeError("Refused unexpected toggle value.")
        if mode != "status":
            wanted = struct.pack("<i", int(mode == "on"))
            error = lldb.SBError()
            count = process.WriteMemory(toggle, wanted, error)
            if error.Fail() or count != 4 or read(process, toggle, 4) != wanted:
                previous = struct.pack("<i", enabled)
                process.WriteMemory(toggle, previous, error)
                if read(process, toggle, 4) != previous:
                    raise RuntimeError("STOP: toggle restoration unconfirmed; do not resume.")
                raise RuntimeError("Toggle write failed; prior value restored.")
            enabled = int(mode == "on")
        reused, recycled, rejected = struct.unpack("<QQQ", read(process, counters, 24))
        result.PutCString(json.dumps(dict(Enabled=bool(enabled), Reused=reused,
                                         Recycled=recycled, RejectedReset=rejected), sort_keys=True))
    except RuntimeError as failure:
        result.SetError(str(failure))


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand("command script add -f " + __name__ + ".command vim-drawable-connections")
