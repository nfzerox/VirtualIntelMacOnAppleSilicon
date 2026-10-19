"""Read-only native/reference legacy parameter-upload comparison."""
import lldb
import struct
import binascii
import os

def enter(frame, location, unused):
    process=frame.GetThread().GetProcess()
    error=lldb.SBError()
    context=frame.FindRegister('rdi').GetValueAsUnsigned()
    state=process.ReadPointerFromMemory(context+0x88,error)
    print('legacy uniforms event='+frame.GetFunctionName())
    for stage in range(5):
        base=state+0x4920+stage*16
        raw=process.ReadMemory(base,16,error)
        if error.Fail():return True
        count=process.ReadUnsignedFromMemory(base+12,4,error)
        ptr=process.ReadPointerFromMemory(base+16,error)
        # Donor reads count at492c, pointer4930 etc; slots overlap this
        # inspection window, not the actual ABI fields.
        print('stage=%d header=%s count=%d pointer=%x' %
              (stage,binascii.hexlify(raw),count,ptr))
        if count and ptr and count<=64:
            values=process.ReadMemory(ptr,count*16,error)
            if error.Success():
                print('values='+repr(struct.unpack('<%df' % (count*4),values)))
            else:print('values unreadable='+str(error))
        if stage in (0,4):
            program=process.ReadPointerFromMemory(state-0x810+0x9228+stage*8,error)
            if program:
                reflection=process.ReadPointerFromMemory(program+0x68,error)
                if reflection:
                    native=bool(os.environ.get('PVG_NATIVE_LEGACY_AUTO'))
                    count=process.ReadUnsignedFromMemory(reflection+(4 if native else 0),4,error)
                    if error.Fail():return True
                    raw=process.ReadMemory(reflection,(8 if native else 16)+min(count,6)*8,error)
                    print('reflection stage=%d %s' % (stage,binascii.hexlify(raw)))
    return False

def __lldb_init_module(debugger,internal_dict):
    target=debugger.GetSelectedTarget()
    for name in ('GLDContextRec::setRenderProgramUniforms()',):
        bp=target.BreakpointCreateByName(name)
        bp.SetScriptCallbackFunction(__name__+'.enter')
