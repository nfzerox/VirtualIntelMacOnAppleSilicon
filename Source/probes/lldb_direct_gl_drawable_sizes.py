"""Bounded numeric host-adapter trace; no pixel reads or target code execution.

Import into LLDB, run vim-drawable-sizes, continue during the Finder workload,
then interrupt and detach. After 64 entries the breakpoint disables itself.
This trace perturbs timing; never use its event rate as a performance result.
"""
import lldb

_count = 0
_limit = 64


def entered(frame, location, _):
    global _count
    _count += 1
    values = []
    for name in ("name", "width", "height"):
        value = frame.FindVariable(name)
        values.append(value.GetValue() if value.IsValid() else "unavailable")
    print("DRAWABLE_SIZE %d id=%s width=%s height=%s" % ((_count,) + tuple(values)))
    context = frame.FindVariable("c").Dereference()
    counters = [context.GetChildMemberWithName(key) for key in
                ("storage_allocations", "storage_reuses", "spare_bytes")]
    if all(value.IsValid() for value in counters):
        print("STORAGE allocations=%s reuses=%s cached_bytes=%s" %
              tuple(value.GetValue() for value in counters))
    if _count >= _limit:
        location.GetBreakpoint().SetEnabled(False)
    return False


def install(debugger, command, result, _):
    global _count, _limit
    _count = 0
    try:
        _limit = int(command.strip()) if command.strip() else 64
        if not 1 <= _limit <= 64:
            raise ValueError()
    except ValueError:
        result.SetError("Use an optional entry limit from 1 to 64")
        return
    target = debugger.GetSelectedTarget()
    breakpoint = target.BreakpointCreateByName("DGSelectDrawable")
    if breakpoint.GetNumLocations() != 1:
        target.BreakpointDelete(breakpoint.GetID())
        result.SetError("Expected one host DGSelectDrawable; no trace installed")
        return
    breakpoint.SetScriptCallbackFunction(__name__ + ".entered")
    result.PutCString("%d-entry numeric drawable trace installed; detach after workload" % _limit)


def __lldb_init_module(debugger, _):
    debugger.HandleCommand("command script add -f " + __name__ + ".install vim-drawable-sizes")
