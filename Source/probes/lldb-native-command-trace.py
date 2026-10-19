"""LLDB callbacks for tracing AppleParavirt command records without injection.

Load this into an attached WindowServer and call ``pvgtrace.install()``.  The
entry breakpoint remembers allocations returned by getCommandBufferBytes:;
the commit breakpoint copies them only after the serializer has filled them.
This avoids Library Validation and leaves the traced process unmodified.
"""

import lldb
import os
import struct
import time


OUTPUT = "/tmp/pvg-lldb-command-trace"
COMMIT_LIMIT = 300
pending_returns = {}
allocations = {}
commit_count = 0
target = None


def _register(frame, name):
    return frame.FindRegister(name).GetValueAsUnsigned()


def _read(process, address, size):
    error = lldb.SBError()
    data = process.ReadMemory(address, size, error)
    return data if error.Success() else b""


def on_get_entry(frame, _location, _dict):
    process = frame.GetThread().GetProcess()
    command_buffer = _register(frame, "rdi")
    length = _register(frame, "rdx")
    stack = _register(frame, "rsp")
    return_bytes = _read(process, stack, 8)
    if len(return_bytes) != 8 or length == 0 or length > (1 << 20):
        return False
    return_address = struct.unpack("<Q", return_bytes)[0]
    thread_id = frame.GetThread().GetThreadID()
    pending_returns[thread_id] = (command_buffer, length)
    breakpoint = target.BreakpointCreateByAddress(return_address)
    breakpoint.SetOneShot(True)
    breakpoint.SetThreadID(thread_id)
    breakpoint.SetScriptCallbackFunction("pvgtrace.on_get_return")
    return False


def on_get_return(frame, _location, _dict):
    thread_id = frame.GetThread().GetThreadID()
    pending = pending_returns.pop(thread_id, None)
    if pending:
        command_buffer, length = pending
        address = _register(frame, "rax")
        if address:
            allocations.setdefault(command_buffer, []).append((address, length))
    return False


def on_commit(frame, _location, _dict):
    global commit_count
    process = frame.GetThread().GetProcess()
    command_buffer = _register(frame, "rdi")
    records = allocations.pop(command_buffer, [])
    sequence = commit_count
    commit_count += 1
    os.makedirs(OUTPUT, exist_ok=True)
    with open(os.path.join(OUTPUT, "manifest.txt"), "a") as manifest:
        manifest.write(
            "commit sequence={} time={:.6f} commandBuffer=0x{:x} records={}\n".format(
                sequence, time.time(), command_buffer, len(records)
            )
        )
        for record_index, (address, length) in enumerate(records):
            data = _read(process, address, length)
            if len(data) != length:
                continue
            opcode = struct.unpack_from("<I", data, 0)[0] if length >= 4 else 0
            wire_length = struct.unpack_from("<I", data, 4)[0] if length >= 8 else 0
            name = "commit-{:03d}-record-{:03d}-{}-0x{:x}.bin".format(
                sequence, record_index, length, opcode
            )
            with open(os.path.join(OUTPUT, name), "wb") as output:
                output.write(data)
            manifest.write(
                "record index={} size={} opcode=0x{:x} wireLength={} path={}\n".format(
                    record_index, length, opcode, wire_length, name
                )
            )
    # Returning true stops `continue`; the command file then detaches.
    return commit_count >= COMMIT_LIMIT


def install():
    global target
    target = lldb.debugger.GetSelectedTarget()
    def implementation(selector):
        expression = (
            '(unsigned long long)((void *(*)(void *, void *))'
            'class_getMethodImplementation)('
            '((void *(*)(const char *))objc_getClass)('
            '"AppleParavirtCommandBuffer"), '
            '((void *(*)(const char *))sel_registerName)("{}"))'
        ).format(selector)
        return target.EvaluateExpression(expression).GetValueAsUnsigned()

    get_address = implementation("getCommandBufferBytes:")
    commit_address = implementation("commit")
    get_bytes = target.BreakpointCreateByAddress(get_address)
    get_bytes.SetScriptCallbackFunction("pvgtrace.on_get_entry")
    commit = target.BreakpointCreateByAddress(commit_address)
    commit.SetScriptCallbackFunction("pvgtrace.on_commit")
    print(
        "PVG trace installed get=0x{:x}/{} commit=0x{:x}/{} limit={}".format(
            get_address,
            get_bytes.GetNumLocations(),
            commit_address,
            commit.GetNumLocations(),
            COMMIT_LIMIT,
        )
    )
