"""Same-boot typed state-query A/B in the owned Mavericks GL client.

Python 2-compatible. No inferior calls, environment writes or OS patches.
"""
import lldb
import struct


def address(target, name):
    matches = target.FindSymbols(name, lldb.eSymbolTypeData)
    found = set()
    for i in range(matches.GetSize()):
        entry = matches.GetContextAtIndex(i)
        if entry.GetModule().GetFileSpec().GetFilename() != "libVIMPassthroughOpenGL.dylib":
            continue
        value = entry.GetSymbol().GetStartAddress().GetLoadAddress(target)
        if value != lldb.LLDB_INVALID_ADDRESS:
            found.add(value)
    if len(found) != 1:
        raise RuntimeError("Expected one owned-client symbol: " + name)
    return next(iter(found))


def command(debugger, text, result, internal_dict):
    del internal_dict
    value = text.strip()
    neutral = value in ("neutral-off", "neutral-on")
    prefetch = value in ("prefetch-off", "prefetch-on")
    trap = value in ("trap-off", "trap-on")
    full_read = value in ("full-read-off", "full-read-on")
    if value not in ("0", "1", "status", "neutral-off", "neutral-on", "prefetch-off", "prefetch-on", "trap-off", "trap-on", "full-read-off", "full-read-on"):
        result.SetError("Use vim-common-state 0, 1, status, neutral-off/on, prefetch-off/on, trap-off/on or full-read-off/on.")
        return
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    if target.GetExecutable().GetFilename() != "WindowServer" or process.GetState() != lldb.eStateStopped:
        result.SetError("Attach to the stopped guest WindowServer first.")
        return
    try:
        setting_name = "dg_prepared_full_read" if full_read else "dg_exchange_trap_override" if trap else "dg_error_state_prefetch" if prefetch else "dg_common_neutral_reuse" if neutral else "dg_common_state_reuse"
        setting = address(target, setting_name)
        error = lldb.SBError()
        previous = process.ReadMemory(setting, 4, error)
        if error.Fail() or len(previous) != 4 or struct.unpack("<I", previous)[0] not in (0, 1):
            raise RuntimeError("Invalid query switch; no write made.")
        if value != "status":
            written = int(value.endswith("-on")) if neutral or prefetch or trap or full_read else int(value)
            data = struct.pack("<I", written)
            if process.WriteMemory(setting, data, error) != 4 or error.Fail() or process.ReadMemory(setting, 4, error) != data:
                raise RuntimeError("Query switch write could not be verified.")
        counters = {}
        for name in ("dg_common_state_hits", "dg_common_state_fills"):
            data = process.ReadMemory(address(target, name), 8, error)
            if error.Fail() or len(data) != 8:
                raise RuntimeError("Cannot read owned-client counter: " + name)
            counters[name] = struct.unpack("<Q", data)[0]
        # Older qualified clients have no prefetch counter. Their existing
        # status/neutral commands must remain usable for comparisons.
        try:
            optional = address(target, "dg_common_state_prefetches")
        except RuntimeError:
            optional = None
        if optional is not None:
            data = process.ReadMemory(optional, 8, error)
            if error.Fail() or len(data) != 8:
                raise RuntimeError("Cannot read owned-client prefetch counter.")
            counters["dg_common_state_prefetches"] = struct.unpack("<Q", data)[0]
        result.PutCString("%s %d -> %s; %s; detach before timing" %
                          (setting_name, struct.unpack("<I", previous)[0], value, counters))
    except RuntimeError as failure:
        result.SetError(str(failure))


def __lldb_init_module(debugger, internal_dict):
    del internal_dict
    debugger.HandleCommand("command script add -f lldb_legacy_common_state.command vim-common-state")
