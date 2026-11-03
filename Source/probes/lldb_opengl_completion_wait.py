"""Same-boot owned-renderer A/B, no inferior calls or host-framework edits.

vim-completion-wait status|on|off|all|render EXPECTED_EXECUTOR_UUID
Only the stopped research QEMU and exact loaded own executor may be changed.
Both modes retain actual fence completion; this is not a skip-wait switch.
on=background queue only (default), all=both queues, render=writing queue only.
"""
import json
import lldb
import struct


def command(debugger, text, result, internal_dict):
    parts = text.split()
    if len(parts) != 2 or parts[0] not in ("status", "on", "off", "all", "render"):
        result.SetError("Use vim-completion-wait status|on|off|all|render EXPECTED_EXECUTOR_UUID")
        return
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    if target.GetExecutable().GetFilename() != "qemu-system-x86_64" or process.GetState() != lldb.eStateStopped:
        result.SetError("Attach and stop the owned research QEMU first; no write.")
        return
    modules = [target.GetModuleAtIndex(i) for i in range(target.GetNumModules())
               if target.GetModuleAtIndex(i).GetFileSpec().GetFilename() == "libVIMPassthroughOpenGLHost.dylib"]
    wanted = parts[1].replace("-", "").upper()
    if len(modules) != 1 or (modules[0].GetUUIDString() or "").replace("-", "").upper() != wanted:
        result.SetError("Refused unmatched research executor; no write.")
        return
    symbols = modules[0].FindSymbols("dg_completion_wait_policy")
    locations = {symbols.GetContextAtIndex(i).GetSymbol().GetStartAddress().GetLoadAddress(target)
                 for i in range(symbols.GetSize())}
    locations.discard(lldb.LLDB_INVALID_ADDRESS)
    if len(locations) != 1:
        result.SetError("Missing or ambiguous owned diagnostic flag; no write.")
        return
    address = next(iter(locations))

    def read():
        error = lldb.SBError()
        data = process.ReadMemory(address, 4, error)
        if error.Fail() or len(data) != 4:
            raise RuntimeError("Cannot read diagnostic state.")
        value = struct.unpack("<I", data)[0]
        if value not in (0, 1, 2, 3):
            raise RuntimeError("Unexpected diagnostic state; no write.")
        return value

    try:
        value = read()
        if parts[0] != "status":
            expected = {"off": 0, "on": 1, "render": 2, "all": 3}[parts[0]]
            error = lldb.SBError()
            if process.WriteMemory(address, struct.pack("<I", expected), error) != 4 or error.Fail():
                raise RuntimeError("Diagnostic write failed; inspect before resuming.")
            value = read()
            if value != expected:
                raise RuntimeError("Diagnostic readback failed; inspect before resuming.")
        result.PutCString(json.dumps({"BlockingCompletion": bool(value&1),
            "BlockingRender": bool(value&2), "ActualFenceRequired": True}))
    except RuntimeError as failure:
        result.SetError(str(failure))


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand("command script add -f " + __name__ + ".command vim-completion-wait")
