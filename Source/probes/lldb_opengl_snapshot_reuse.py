"""Owned-executor same-boot A/B. No inferior calls or security changes.

Import in LLDB, run vim-snapshot-reuse 0/1/status, then detach before measuring.
This changes allocation reuse only, never fences, versions or CPU ownership.
"""
import lldb
import struct


def address(target, name):
    matches = target.FindSymbols(name, lldb.eSymbolTypeData)
    found = set()
    for i in range(matches.GetSize()):
        entry = matches.GetContextAtIndex(i)
        if entry.GetModule().GetFileSpec() != target.GetExecutable():
            continue
        value = entry.GetSymbol().GetStartAddress().GetLoadAddress(target)
        if value != lldb.LLDB_INVALID_ADDRESS:
            found.add(value)
    if len(found) != 1:
        raise RuntimeError("Expected one owned-executor symbol: " + name)
    return next(iter(found))


def command(debugger, text, result, internal_dict):
    del internal_dict
    value = text.strip()
    if value not in ("0", "1", "status"):
        result.SetError("Use vim-snapshot-reuse 0, 1 or status.")
        return
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    if not (target.GetExecutable().GetFilename() or "").startswith("passthrough-opengl-host") or process.GetState() != lldb.eStateStopped:
        result.SetError("Attach to a stopped, owned passthrough-opengl-host executor.")
        return
    try:
        setting = address(target, "dg_snapshot_storage_reuse")
        error = lldb.SBError()
        previous = process.ReadMemory(setting, 4, error)
        if error.Fail() or len(previous) != 4 or struct.unpack("<I", previous)[0] not in (0, 1):
            raise RuntimeError("Invalid reuse switch; no write made.")
        if value != "status":
            data = struct.pack("<I", int(value))
            if process.WriteMemory(setting, data, error) != 4 or error.Fail() or process.ReadMemory(setting, 4, error) != data:
                raise RuntimeError("Reuse switch write could not be verified.")
        counters = {}
        for name in ("dg_snapshot_storage_allocations", "dg_snapshot_storage_reuses", "dg_snapshot_bytes", "dg_snapshot_spare_bytes"):
            data = process.ReadMemory(address(target, name), 8, error)
            if error.Fail() or len(data) != 8:
                raise RuntimeError("Cannot read owned-executor counter: " + name)
            counters[name] = struct.unpack("<Q", data)[0]
        result.PutCString("snapshot reuse %d -> %s; %s; detach before timing" %
                          (struct.unpack("<I", previous)[0], value, counters))
    except RuntimeError as failure:
        result.SetError(str(failure))


def __lldb_init_module(debugger, internal_dict):
    del internal_dict
    debugger.HandleCommand("command script add -f lldb_legacy_snapshot_reuse.command vim-snapshot-reuse")
