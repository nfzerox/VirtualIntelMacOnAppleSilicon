"""Read-only QEMU kernel capture when the running kernel differs from a KDK.

LLDB: command script import /absolute/path/lldb_opengl_kernel.py
      vim-dump-kernel HEADER_ADDRESS NEW_OUTPUT_PATH
Reads the guest via the existing GDB connection. Never writes guest memory.
"""
import lldb
import shlex
import struct


def dump_kernel(debugger, command, result, _internal):
    try:
        address, output = shlex.split(command)
        base = int(address, 0)
        process = debugger.GetSelectedTarget().GetProcess()

        def read(addr, size):
            error = lldb.SBError()
            data = process.ReadMemory(addr, size, error)
            if not error.Success() or len(data) != size:
                raise RuntimeError(f"read {addr:#x}+{size:#x}: {error}")
            return data

        header = read(base, 32)
        magic, _, _, _, count, size, _, _ = struct.unpack("<8I", header)
        if magic != 0xFEEDFACF or count > 128 or size > 65536:
            raise ValueError("not a bounded 64-bit Mach-O kernel header")
        commands = read(base + 32, size)
        offset, segments = 0, []
        for _ in range(count):
            cmd, length = struct.unpack_from("<II", commands, offset)
            if length < 8 or offset + length > size:
                raise ValueError("invalid Mach-O command bounds")
            if cmd == 0x19:
                name, va, vs, fileoff, filesize = struct.unpack_from("<16s4Q", commands, offset + 8)
                name = name.rstrip(b"\0").decode("ascii")
                if filesize:
                    segments.append((name, va, fileoff, filesize))
            offset += length
        text = next(s for s in segments if s[0] == "__TEXT")
        slide = base - text[1]
        total = max(s[2] + s[3] for s in segments)
        if total > 128 * 1024 * 1024:
            raise ValueError("kernel image exceeds research capture limit")
        with open(output, "xb") as stream:
            stream.truncate(total)
            for name, va, fileoff, filesize in segments:
                result.AppendMessage(f"{name}: memory={va+slide:#x} file={fileoff:#x}+{filesize:#x}")
                stream.seek(fileoff)
                for position in range(0, filesize, 1024 * 1024):
                    length=min(filesize-position, 1024*1024)
                    try:
                        data=read(va+slide+position,length)
                    except RuntimeError as error:
                        result.AppendMessage("UNMAPPED (left sparse): "+str(error))
                        stream.seek(length,1)
                    else:
                        stream.write(data)
        result.AppendMessage(f"Captured mapped portions; slide={slide:#x}. Runtime fixups remain applied; freed segments are holes.")
    except Exception as error:
        result.SetError(str(error))


def __lldb_init_module(debugger, _internal):
    debugger.HandleCommand("command script add -f lldb_legacy_kernel.dump_kernel vim-dump-kernel")
    debugger.HandleCommand("command script add -f lldb_legacy_kernel.panic_text vim-kernel-panic")
    debugger.HandleCommand("command script add -f lldb_legacy_kernel.message_buffer vim-kernel-messages")


def panic_text(debugger, command, result, _internal):
    target=debugger.GetSelectedTarget()
    matches=target.FindSymbols("debug_buf")
    if not matches.GetSize():
        result.SetError("debug_buf is absent; load the exact running kernel symbols")
        return
    symbol=matches.GetContextAtIndex(0).GetSymbol()
    address=symbol.GetStartAddress().GetLoadAddress(target)
    error=lldb.SBError()
    text=target.GetProcess().ReadCStringFromMemory(address,16384,error)
    if not error.Success():
        result.SetError(str(error))
    else:
        result.AppendMessage(text or "No panic text.")


def message_buffer(debugger, command, result, _internal):
    """Read the 10.9 sys/msgbuf.h ring; exact kernel symbols are required."""
    try:
        target = debugger.GetSelectedTarget()
        process = target.GetProcess()
        matches = target.FindSymbols("msgbufp")
        if not matches.GetSize():
            raise RuntimeError("msgbufp is absent; load the exact running kernel symbols")
        address = matches.GetContextAtIndex(0).GetSymbol().GetStartAddress().GetLoadAddress(target)
        error = lldb.SBError()
        pointer = process.ReadPointerFromMemory(address, error)
        if error.Fail():
            raise RuntimeError(str(error))
        header = process.ReadMemory(pointer, 24, error)
        if error.Fail():
            raise RuntimeError(str(error))
        magic, size, write, read, data = struct.unpack("<4iQ", header)
        if magic != 0x063061 or not 0 < size <= 1024 * 1024 or not 0 <= write < size:
            raise RuntimeError("invalid Mavericks message-ring bounds")
        raw = process.ReadMemory(data, size, error)
        if error.Fail():
            raise RuntimeError(str(error))
        result.AppendMessage((raw[write:] + raw[:write]).replace(b"\0", b"").decode("utf-8", "replace"))
    except Exception as error:
        result.SetError(str(error))
