"""Read-only, bounded owned-app drawable disposition trace.

Use only with the matched x64 research UI. Records already-computed marker/
fingerprint metadata, not pixels. No inferior calls, memory writes or host
security changes. Attach/install/continue, run a finite held-step workload,
interrupt, run vim-presented-frames status, detach. Debugger timing is NOT a
clean latency/FPS measurement. An unpresented marker is never a visible frame.
"""
import json
import struct
import time
import lldb

UUID = "6FECEBD9FF183E8A8EB6D4292E74B93A"
FIELDS = {
    "gpuReady": "B", "presentationReady": "B", "valid": "B", "consumed": "B",
    "fingerprint": "Q", "regionFingerprint": "Q", "presentedAt": "d",
    "epoch": "Q", "generation": "Q", "width": "Q", "height": "Q", "marker": "I",
}
state = {}


def address(target, name):
    addresses = set()
    for spelling in (name, name[1:] if name.startswith("_") else name):
        matches = target.FindSymbols(spelling)
        for index in range(matches.GetSize()):
            entry = matches.GetContextAtIndex(index)
            if (entry.GetModule().GetUUIDString() or "").replace("-", "").upper() != UUID:
                continue
            value = entry.GetSymbol().GetStartAddress().GetLoadAddress(target)
            if value != lldb.LLDB_INVALID_ADDRESS:
                addresses.add(value)
    if len(addresses) != 1:
        raise RuntimeError("Expected one matched own-app symbol: " + name)
    return next(iter(addresses))


def read(process, location, fmt):
    size = struct.calcsize("<" + fmt)
    error = lldb.SBError()
    data = process.ReadMemory(location, size, error)
    if error.Fail() or len(data) != size:
        raise RuntimeError("Cannot read matched owned metadata.")
    return struct.unpack("<" + fmt, data)[0]


def observe(frame, bp_loc, internal_dict):
    if state.get("error") or len(state.get("rows", [])) >= 128:
        return False
    try:
        process = frame.GetThread().GetProcess()
        pointer = frame.FindRegister("rdx").GetValueAsUnsigned()
        if not pointer:
            raise RuntimeError("Missing owned fingerprint argument.")
        error = lldb.SBError()
        data = process.ReadMemory(pointer, state["bytes"], error)
        if error.Fail() or len(data) != state["bytes"]:
            raise RuntimeError("Cannot read bounded owned fingerprint metadata.")
        row = {key: struct.unpack_from("<" + fmt, data, state["offsets"][key])[0]
               for key, fmt in FIELDS.items()}
        if row["consumed"] or not row["gpuReady"] or not row["presentationReady"]:
            return False
        row["ObservedAt"] = time.monotonic()
        marker = row.pop("marker")
        row["MarkerX"] = -1 if marker == 0xffffffff else marker & 65535
        row["MarkerY"] = -1 if marker == 0xffffffff else marker >> 16
        state["rows"].append(row)
    except Exception as failure:
        state["error"] = str(failure)
    return False


def command(debugger, text, result, internal_dict):
    mode = text.strip()
    try:
        target = debugger.GetSelectedTarget()
        process = target.GetProcess()
        if process.GetState() != lldb.eStateStopped:
            raise RuntimeError("Stop the owned research app first.")
        if mode == "status":
            result.PutCString(json.dumps(state, sort_keys=True))
            return
        if mode != "install" or state:
            raise RuntimeError("Use install once, then status before detach.")
        if target.GetExecutable().GetFilename() != "Virtual Intel Mac" or not target.GetTriple().startswith("x86_64"):
            raise RuntimeError("Refused a different process/architecture.")
        offsets = {key: read(process, address(target, "_OBJC_IVAR_$_VIMPresentedFingerprint._" + key), "I")
                   for key in FIELDS}
        if len(set(offsets.values())) != len(offsets) or any(not 8 <= value < 256 for value in offsets.values()):
            raise RuntimeError("Unexpected owned Objective-C ivar layout.")
        location = address(target, "-[VIMMetalView resolveFingerprint:]")
        breakpoint = target.BreakpointCreateByAddress(location)
        if breakpoint.GetNumLocations() != 1:
            raise RuntimeError("Cannot resolve owned callback entry.")
        breakpoint.SetScriptCallbackFunction(__name__ + ".observe")
        state.update(offsets=offsets, bytes=max(offsets[key] + struct.calcsize("<" + fmt)
                                              for key, fmt in FIELDS.items()),
                     rows=[], error=None, breakpoint=breakpoint.GetID())
        result.PutCString("READY bounded read-only drawable metadata (128 rows maximum)")
    except Exception as failure:
        result.SetError(str(failure))


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand("command script add -f " + __name__ + ".command vim-presented-frames")
