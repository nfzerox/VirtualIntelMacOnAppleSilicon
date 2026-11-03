"""Read-only scheduler flags for the matched 13F1911 CoreGraphics.

Works with Mavericks' Python 2 LLDB and modern LLDB. Resolve actual loaded
symbol addresses through LLDB, not file-offset-plus-guessed-shared-cache-slide.
Run attached/stopped, then detach before measuring or driving UI workloads.
No preferences, memory, timings, processes or security policy are modified.
"""
import json
import lldb

UUID = "ED5E3C377E2F38858091DFC9B44B38E8"
FIELDS = ("gDeferredUpdatesPref", "gDeferredUpdates", "gForceDeferredUpdates",
          "gTripleBufferUpdates", "gOpenLoopScheduler")


def scheduler_state(debugger, command, result, internal_dict):
    if command.strip():
        result.SetError("This command is read-only and accepts no arguments.")
        return
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    if process.GetState() != lldb.eStateStopped:
        result.SetError("Attach and stop before reading the scheduler, then detach before workloads.")
        return
    rows = {}
    for name in FIELDS:
        matches = target.FindSymbols(name)
        addresses = set()
        for index in range(matches.GetSize()):
            context = matches.GetContextAtIndex(index)
            module = context.GetModule()
            actual = (module.GetUUIDString() or "").replace("-", "").upper()
            if module.GetFileSpec().GetFilename() != "CoreGraphics" or actual != UUID:
                continue
            address = context.GetSymbol().GetStartAddress().GetLoadAddress(target)
            if address != lldb.LLDB_INVALID_ADDRESS:
                addresses.add(address)
        if len(addresses) != 1:
            result.SetError("Expected one verified loaded CoreGraphics symbol: " + name)
            return
        address = next(iter(addresses))
        error = lldb.SBError()
        data = process.ReadMemory(address, 1, error)
        if not error.Success() or len(data) != 1:
            result.SetError("Cannot read verified scheduler byte: " + name)
            return
        value = data[0] if isinstance(data[0], int) else ord(data[0])
        rows[name] = dict(Address=hex(address), Value=value)
    result.AppendMessage(json.dumps(rows, sort_keys=True))


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand("command script add -f " + __name__ + ".scheduler_state vim-scheduler-state")
