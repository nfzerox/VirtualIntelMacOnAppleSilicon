"""LLDB-only local A/B control; no guest code execution or fixed offsets.

lldb -b -p PID -o 'command script import probes/tcg_jmp_cache_mode.py' \
  -o 'tcg-jmp-cache-mode on' -o 'process detach' -o quit

Requires the generation-tagged QEMU candidate and its debug information.
Both modes continue checking generations, so switching modes does not make
an expired entry valid. Only switch with all process threads stopped.
"""
import lldb


def set_mode(debugger, command, result, internal_dict):
    if command.strip() not in ("on", "off"):
        result.SetError("usage: tcg-jmp-cache-mode on|off")
        return
    target = debugger.GetSelectedTarget()
    if target.GetProcess().GetState() != lldb.eStateStopped:
        result.SetError("All process threads must be stopped first")
        return
    cpu = target.FindFirstGlobalVariable("cpus_queue").GetChildMemberWithName("tqh_first")
    flags = []
    seen = set()
    while cpu.IsValid() and cpu.GetValueAsUnsigned():
        address = cpu.GetValueAsUnsigned()
        if address in seen or len(seen) >= 64:
            result.SetError("Unexpected CPU list; no changes made")
            return
        seen.add(address)
        state = cpu.Dereference()
        cache = state.GetChildMemberWithName("tb_jmp_cache").Dereference()
        flag = cache.GetChildMemberWithName("lazy_flush")
        if not flag.IsValid() or flag.GetByteSize() != 1:
            result.SetError("Candidate debug layout missing; no changes made")
            return
        flags.append(flag)
        cpu = state.GetChildMemberWithName("node").GetChildMemberWithName("tqe_next")
    if not flags or not cpu.IsValid():
        result.SetError("Incomplete CPU list; no changes made")
        return
    value = "true" if command.strip() == "on" else "false"
    for flag in flags:
        error = lldb.SBError()
        if not flag.SetValueFromCString(value, error):
            result.SetError(f"Write failed after possible partial change: {error}")
            return
    result.AppendMessage(f"Lazy jump-cache invalidation {command.strip()}: {len(flags)} CPUs")


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand(f"command script add -f {__name__}.set_mode tcg-jmp-cache-mode")
