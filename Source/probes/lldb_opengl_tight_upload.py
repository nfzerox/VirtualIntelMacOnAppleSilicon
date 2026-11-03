"""Owned guest-client upload A/B; no inferior calls or persistent settings."""
import lldb
import struct


def address(target, name):
    matches = target.FindSymbols(name, lldb.eSymbolTypeData)
    found = set()
    for i in range(matches.GetSize()):
        entry = matches.GetContextAtIndex(i)
        if entry.GetModule().GetFileSpec().GetFilename() == "libVIMPassthroughOpenGL.dylib":
            value = entry.GetSymbol().GetStartAddress().GetLoadAddress(target)
            if value != lldb.LLDB_INVALID_ADDRESS:
                found.add(value)
    if len(found) != 1:
        raise RuntimeError("Expected one owned-client symbol: " + name)
    return next(iter(found))


def command(debugger, text, result, internal_dict):
    del internal_dict
    value = text.strip()
    if value not in ("0", "1", "status"):
        result.SetError("Use vim-tight-upload 0, 1 or status.")
        return
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    if target.GetExecutable().GetFilename() != "WindowServer" or process.GetState() != lldb.eStateStopped:
        result.SetError("Attach to the stopped guest WindowServer first.")
        return
    try:
        setting = address(target, "dg_tight_uploads")
        error = lldb.SBError()
        previous = process.ReadMemory(setting, 4, error)
        if error.Fail() or len(previous) != 4 or struct.unpack("<I", previous)[0] not in (0, 1):
            raise RuntimeError("Invalid upload switch; no write made.")
        if value != "status":
            data = struct.pack("<I", int(value))
            if process.WriteMemory(setting, data, error) != 4 or error.Fail() or process.ReadMemory(setting, 4, error) != data:
                raise RuntimeError("Upload switch write could not be verified.")
        counters = {}
        for suffix in ("calls", "source_bytes", "payload_bytes", "gathers", "copies"):
            name = "dg_tight_upload_" + suffix
            data = process.ReadMemory(address(target, name), 8, error)
            if error.Fail() or len(data) != 8:
                raise RuntimeError("Cannot read owned-client counter: " + name)
            counters[suffix] = struct.unpack("<Q", data)[0]
        result.PutCString("tight upload %d -> %s; %s; detach before timing" %
                          (struct.unpack("<I", previous)[0], value, counters))
    except RuntimeError as failure:
        result.SetError(str(failure))


def __lldb_init_module(debugger, internal_dict):
    del internal_dict
    debugger.HandleCommand("command script add -f lldb_legacy_tight_upload.command vim-tight-upload")
