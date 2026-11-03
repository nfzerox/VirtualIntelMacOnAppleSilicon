"""Same-boot, owned-host-executor comparison; no inferior function calls.

Attach to passthrough-opengl-host-copy-queue (or another passthrough-opengl-host build), import
this module, run vim-copy-wait 0/1 and detach before timing. This is not an
environment-polled option or permission to skip CPU ownership completion.
"""
import lldb
import struct


def switch(debugger, command, result, internal_dict):
    del internal_dict
    if command.strip() not in ("0", "1"):
        result.SetError("Use vim-copy-wait 0 or 1.")
        return
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    name = target.GetExecutable().GetFilename() or ""
    if not name.startswith("passthrough-opengl-host") or process.GetState() != lldb.eStateStopped:
        result.SetError("Attach to a stopped, owned passthrough-opengl-host executor first.")
        return
    matches = target.FindSymbols("dg_vram_direct_copy_wait", lldb.eSymbolTypeData)
    addresses = {}
    for i in range(matches.GetSize()):
        context = matches.GetContextAtIndex(i)
        module = context.GetModule()
        if module.GetFileSpec() != target.GetExecutable():
            continue
        address = context.GetSymbol().GetStartAddress().GetLoadAddress(target)
        if address != lldb.LLDB_INVALID_ADDRESS:
            addresses[address] = module.GetUUIDString()
    if len(addresses) != 1:
        result.SetError("Expected exactly one executor comparison symbol; no write made.")
        return
    address = list(addresses)[0]
    error = lldb.SBError()
    before = process.ReadMemory(address, 4, error)
    if error.Fail() or len(before) != 4 or struct.unpack("<I", before)[0] not in (0, 1):
        result.SetError("Invalid comparison switch value; no write made.")
        return
    data = struct.pack("<I", int(command.strip()))
    written = process.WriteMemory(address, data, error)
    if error.Fail() or written != 4 or process.ReadMemory(address, 4, error) != data or error.Fail():
        result.SetError("Unable to verify comparison write.")
        return
    result.PutCString("copy-queue completion %d -> %s; executor UUID %s; detach before timing" %
                      (struct.unpack("<I", before)[0], command.strip(), addresses[address]))


def __lldb_init_module(debugger, internal_dict):
    del internal_dict
    debugger.HandleCommand("command script add -f lldb_legacy_copy_wait.switch vim-copy-wait")
