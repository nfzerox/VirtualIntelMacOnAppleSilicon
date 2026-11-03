"""Bounded same-boot comparison of the owned research GLI's compound flush.

Attach to the guest WindowServer, import this module, run
vim-compound-flush 0 (old path) or 1 (default), then detach immediately.
Never calls functions in the stopped process, changes launch environment,
or writes an OS binary. Compatible with Mavericks' Python 2 LLDB.
"""
import lldb
import struct


def switch(debugger, command, result, internal_dict):
    del internal_dict
    if command.strip() not in ("0", "1"):
        result.SetError("Use vim-compound-flush 0 or 1.")
        return
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    if process.GetState() != lldb.eStateStopped:
        result.SetError("The owned guest process must be stopped first.")
        return
    if target.GetExecutable().GetFilename() != "WindowServer":
        result.SetError("This comparison is scoped to the guest WindowServer.")
        return
    matches = target.FindSymbols("vim_compound_flush", lldb.eSymbolTypeData)
    addresses = {}
    for i in range(matches.GetSize()):
        context = matches.GetContextAtIndex(i)
        module = context.GetModule()
        if module.GetFileSpec().GetFilename() != "GLEngine":
            continue
        address = context.GetSymbol().GetStartAddress().GetLoadAddress(target)
        if address != lldb.LLDB_INVALID_ADDRESS:
            addresses[address] = module.GetUUIDString()
    if len(addresses) != 1:
        result.SetError("Expected exactly one owned research GLI switch; no write made.")
        return
    address = list(addresses)[0]
    error = lldb.SBError()
    before = process.ReadMemory(address, 4, error)
    if error.Fail() or len(before) != 4 or struct.unpack("<I", before)[0] not in (0, 1):
        result.SetError("The diagnostic switch does not have a valid current value.")
        return
    data = struct.pack("<I", int(command.strip()))
    written = process.WriteMemory(address, data, error)
    if error.Fail() or written != 4 or process.ReadMemory(address, 4, error) != data or error.Fail():
        result.SetError("Unable to verify the diagnostic switch write.")
        return
    result.PutCString("compound flush %d -> %s; GLEngine UUID %s; detach before measuring" %
                      (struct.unpack("<I", before)[0], command.strip(), addresses[address]))


def __lldb_init_module(debugger, internal_dict):
    del internal_dict
    debugger.HandleCommand("command script add -f lldb_legacy_compound_flush.switch vim-compound-flush")
