"""Paused, exact-build GLI scope comparison; no inferior calls or polling.

vim-backing-scope status|on|off EXPECTED_GLI_UUID
Only the owned-driver guest WindowServer is eligible, never physical Intel.
An off selection affects future attaches, not any already active GPU loan.
"""
import json
import struct
import lldb


def command(debugger, text, result, internal_dict):
    args = text.split()
    if len(args) != 2 or args[0] not in ("on", "off", "status"):
        result.SetError("Use vim-backing-scope status|on|off EXPECTED_GLI_UUID")
        return
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    if target.GetExecutable().GetFilename() != "WindowServer" or process.GetState() != lldb.eStateStopped:
        result.SetError("Stop the owned guest WindowServer first.")
        return
    expected = args[1].replace("-", "").upper()
    try:
        if len(expected) != 32 or any(c not in "0123456789ABCDEF" for c in expected):
            raise RuntimeError("Require the actual freshly built GLI UUID.")

        def address(name):
            matches = target.FindSymbols(name)
            found = set()
            for i in range(matches.GetSize()):
                item = matches.GetContextAtIndex(i)
                module = item.GetModule()
                if module.GetFileSpec().GetFilename() != "GLEngine":
                    continue
                if (module.GetUUIDString() or "").replace("-", "").upper() != expected:
                    raise RuntimeError("Refused different loaded GLI UUID; no write.")
                value = item.GetSymbol().GetStartAddress().GetLoadAddress(target)
                if value != lldb.LLDB_INVALID_ADDRESS:
                    found.add(value)
            if len(found) != 1:
                raise RuntimeError("Require one matched owned-GLI symbol: " + name)
            return next(iter(found))

        def read(where, count):
            error = lldb.SBError()
            data = process.ReadMemory(where, count, error)
            if error.Fail() or len(data) != count:
                raise RuntimeError("Unable to read complete paused scope state.")
            return data

        flag = address("vim_backing_render_scopes")
        stats = address("vim_backing_render_stats")
        before = struct.unpack("<I", read(flag, 4))[0]
        if before not in (0, 1):
            raise RuntimeError("Refused invalid selection state.")
        if args[0] != "status":
            wanted = struct.pack("<I", int(args[0] == "on"))
            error = lldb.SBError()
            if process.WriteMemory(flag, wanted, error) != 4 or error.Fail() or read(flag, 4) != wanted:
                raise RuntimeError("Selection write/verification failed; inspect before resume.")
        attempts, begins, ends, unsupported = struct.unpack("<4Q", read(stats, 32))
        result.PutCString(json.dumps(dict(Enabled=struct.unpack("<I", read(flag, 4))[0],
            Attempts=attempts, Begins=begins, Ends=ends, Unsupported=unsupported,
            GLIUUID=expected), sort_keys=True))
    except RuntimeError as error:
        result.SetError(str(error))


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand("command script add -f " + __name__ + ".command vim-backing-scope")
