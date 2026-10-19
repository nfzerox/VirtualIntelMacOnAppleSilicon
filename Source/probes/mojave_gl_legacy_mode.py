"""Research only: scope a config-mode override to legacy state generation.
No file patches; never enable this in a desktop process.
"""
import lldb
import os
import struct
pending={}

def restore(frame,loc,unused):
    key=loc.GetBreakpoint().GetID()
    if key not in pending:return False
    address,value=pending.pop(key)
    error=lldb.SBError()
    frame.GetThread().GetProcess().WriteMemory(address,struct.pack('B',value),error)
    print('legacy mode restored=%d ok=%s' % (value,error.Success()))
    return not error.Success()

def enter(frame,loc,unused):
    process=frame.GetThread().GetProcess()
    error=lldb.SBError()
    ctx=frame.FindRegister('rdi').GetValueAsUnsigned()
    config=process.ReadPointerFromMemory(ctx+0xb0b8,error)
    if not error.Success() or not config:return True
    value=process.ReadUnsignedFromMemory(config+0x19,1,error)
    mode=int(os.environ.get('PVG_LEGACY_MODE','1'))
    if not error.Success() or mode not in (1,2):return True
    address=config+0x19
    replacement=(value&0x80)|mode
    if os.environ.get('PVG_LEGACY_DUAL'):
        # Matched native update path: mode zero plus this internal flag
        # generates LLVM IR, then PP. Research address, not deployment ABI.
        address=ctx+0xb2dc
        value=process.ReadUnsignedFromMemory(address,1,error)
        if not error.Success():return True
        replacement=1
    # LLDB symbol breakpoints stop after the prologue; unwind to caller PC.
    caller=frame.GetThread().GetFrameAtIndex(1).GetPC()
    bp=process.GetTarget().BreakpointCreateByAddress(caller)
    bp.SetThreadID(frame.GetThread().GetThreadID())
    bp.SetOneShot(True)
    bp.SetScriptCallbackFunction(__name__+'.restore')
    pending[bp.GetID()]=(address,value)
    process.WriteMemory(address,struct.pack('B',replacement),error)
    print('legacy mode %d -> %d function=%s ok=%s' %
          (value,mode,frame.GetFunctionName(),error.Success()))
    return not error.Success()

def __lldb_init_module(debugger,internal_dict):
    names=('gleUpdateContextStateBuffers',) if os.environ.get('PVG_LEGACY_BUFFERS_ONLY') else ('gleUpdateVertexStateProgram','gleUpdateFragmentStateProgram')
    for name in names:
        bp=debugger.GetSelectedTarget().BreakpointCreateByName(name)
        bp.SetScriptCallbackFunction(__name__+'.enter')
