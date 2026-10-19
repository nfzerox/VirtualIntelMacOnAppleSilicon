"""LLDB read-only snapshots of running TCG PCs and contemporaneous ARM bytes.

Attach, import this file, run `tcg-live-instructions 30`, then detach.
Stops perturb scheduling: this is instruction attribution, NOT latency timing.
No inferior expressions/function calls and no target memory writes.
"""
import json
import time
import struct
import lldb


def find_block(target, pc):
    """Read the balanced region index without invoking lock-taking C code."""
    def field(value, name):
        child = value.GetChildMemberWithName(name)
        if not child.IsValid() or child.GetError().Fail():
            raise RuntimeError('missing debug field ' + name)
        return child
    region = target.FindFirstGlobalVariable('region')
    count = field(region, 'n').GetValueAsUnsigned()
    trees = target.FindFirstGlobalVariable('region_trees').GetValueAsUnsigned()
    stride = target.FindFirstGlobalVariable('tree_size').GetValueAsUnsigned()
    rt_type = target.FindFirstType('tcg_region_tree')
    tb_type = target.FindFirstType('TranslationBlock')
    process = target.GetProcess()
    def read(address, size):
        error = lldb.SBError()
        data = process.ReadMemory(address, size, error)
        if error.Fail() or len(data) != size:
            raise RuntimeError('index memory read failed')
        return data
    if not 0 < count <= 256 or not trees or not stride or not rt_type.IsValid() or not tb_type.IsValid():
        raise RuntimeError('region debug layout unavailable')
    # Try all region roots: avoids assumptions about executable/RW aliases.
    for index in range(count):
        rt = target.CreateValueFromAddress('rt', lldb.SBAddress(trees+index*stride,target), rt_type)
        # QTree and QTreeNode layout from this fork's util/qtree.c, 64-bit
        # host only. Avoid LLDB's crashing resolution of the opaque typedef.
        node = struct.unpack('<Q', read(field(rt,'tree').GetValueAsUnsigned(),8))[0]
        for depth in range(64):
            if not node:
                break
            key, value, left, right, balance, has_left, has_right = struct.unpack('<QQQQbBB',read(node,35))
            tb = target.CreateValueFromAddress('tb',lldb.SBAddress(value,target),tb_type)
            tc = field(tb, 'tc')
            start = field(tc, 'ptr').GetValueAsUnsigned()
            size = field(tc, 'size').GetValueAsUnsigned()
            if start <= pc < start+size:
                return dict(start=hex(start), size=size,
                            tb=hex(value),
                            guest_pc=hex(field(tb,'pc').GetValueAsUnsigned()),
                            cflags=hex(field(tb,'cflags').GetValueAsUnsigned()),
                            guest_instructions=field(tb,'icount').GetValueAsUnsigned())
            if not (has_left if pc < start else has_right):
                break
            node = left if pc < start else right
    return None


def capture(debugger, command, result, internal_dict):
    count = int(command.strip() or "20")
    if not 1 <= count <= 100:
        result.SetError("snapshot count must be 1..100")
        return
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    if process.GetState() != lldb.eStateStopped:
        result.SetError("attach with all threads stopped first")
        return
    previous_async = debugger.GetAsync()
    # QEMU's sigaltstack coroutine constructor requires this signal to be
    # delivered. LLDB swallowing it can wedge a vCPU while holding the BQL.
    debugger.HandleCommand('process handle SIGUSR2 -s false -n false -p true')
    debugger.SetAsync(True)
    try:
        for iteration in range(count):
            for thread in process:
                name = thread.GetName() or ""
                if "/TCG" not in name:
                    continue
                frame = thread.GetFrameAtIndex(0)
                pc = frame.GetPC()
                address = target.ResolveLoadAddress(pc)
                module = address.GetModule()
                record = dict(iteration=iteration, thread=name, pc=hex(pc),
                              module=str(module.GetFileSpec()) if module.IsValid() else None)
                if not module.IsValid():
                    # The optional private-index walker remains unqualified.
                    # Keep the proven byte capture independent of debug types.
                    start = pc - 64
                    error = lldb.SBError()
                    raw = process.ReadMemory(start, 132, error)
                    if error.Success():
                        record["start"] = hex(start)
                        record["bytes"] = raw.hex()
                        instructions = target.ReadInstructions(target.ResolveLoadAddress(start), 33)
                        record["instructions"] = [
                            [hex(ins.GetAddress().GetLoadAddress(target)),
                             ins.GetMnemonic(target), ins.GetOperands(target)]
                            for ins in instructions]
                    else:
                        record["error"] = str(error)
                result.AppendMessage(json.dumps(record))
            if iteration + 1 < count:
                error = process.Continue()
                if error.Fail():
                    raise RuntimeError(str(error))
                time.sleep(0.113)
                error = process.Stop()
                if error.Fail():
                    raise RuntimeError(str(error))
                deadline = time.monotonic() + 3
                while process.GetState() != lldb.eStateStopped and time.monotonic() < deadline:
                    time.sleep(0.01)
                if process.GetState() != lldb.eStateStopped:
                    raise RuntimeError("could not confirm stopped state")
    except Exception as error:
        result.SetError(str(error))
    finally:
        debugger.SetAsync(previous_async)


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand(f"command script add -f {__name__}.capture tcg-live-instructions")
