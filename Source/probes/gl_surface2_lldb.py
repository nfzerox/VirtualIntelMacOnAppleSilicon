"""Bounded diagnostic, not a shipped patch. Attach only to guest WindowServer.

Verified Mojave IOAccelCreateSurface +0x42 calls IOServiceOpen after clearing
edx. Big Sur instead passes (mode >> 9) & 0x20. Test precisely that translation.
Always detach at the end; no memory or on-disk patch is installed.
"""
import lldb
import time

def run(debugger, seconds=25):
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    symbol = target.FindFunctions('IOAccelCreateSurface').GetContextAtIndex(0).GetSymbol()
    start = symbol.GetStartAddress().GetLoadAddress(target)
    error = lldb.SBError()
    code = process.ReadMemory(start + 0x40, 3, error)
    if not error.Success() or code != b'\x31\xd2\xe8':
        process.Detach()
        raise RuntimeError('Mojave IOAccelCreateSurface instruction mismatch')
    bp = target.BreakpointCreateByAddress(start + 0x42)
    debugger.SetAsync(True)
    deadline = time.time() + seconds
    translated = 0
    try:
        process.Continue()
        while time.time() < deadline:
            if process.GetState() == lldb.eStateStopped:
                for thread in process:
                    frame = thread.GetFrameAtIndex(0)
                    if frame.GetPC() == start + 0x42:
                        mode = frame.FindRegister('r15').GetValueAsUnsigned()
                        if mode & 0x4000:
                            frame.FindRegister('edx').SetValueFromCString('32')
                            translated += 1
                process.Continue()
            time.sleep(0.02)
    finally:
        process.Stop()
        target.BreakpointDelete(bp.GetID())
        process.Detach()
        print('Metal surface requests translated:', translated)
