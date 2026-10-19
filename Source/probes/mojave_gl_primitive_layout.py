"""Primitive-buffer trace; optional explicit reference-policy A/B experiment."""
import lldb
import binascii
import os
import struct
returns={}
def returned(frame,loc,unused):
    key=loc.GetBreakpoint().GetID()
    if key not in returns:return False
    ctx=returns.pop(key)
    p=frame.GetThread().GetProcess();e=lldb.SBError()
    print('primitive layout returned stride=%d' % p.ReadUnsignedFromMemory(ctx+0x8a00,8,e))
    return False
def enter(frame,loc,unused):
    p=frame.GetThread().GetProcess();e=lldb.SBError()
    ctx=frame.FindRegister('rdi').GetValueAsUnsigned()
    name=frame.GetFunctionName()
    print('layout event=%s flags=%x mode=%x stride=%d' % (name,
        p.ReadUnsignedFromMemory(ctx+0x3cc,4,e),
        p.ReadUnsignedFromMemory(ctx+0x3e4,4,e),
        p.ReadUnsignedFromMemory(ctx+0x8a00,8,e)))
    print('layout state320=%x primitive=%x dirty=%x' % (
        p.ReadUnsignedFromMemory(ctx+0x320,4,e),
        p.ReadUnsignedFromMemory(ctx+0x3d0,2,e),
        p.ReadUnsignedFromMemory(ctx+0x9420,4,e)))
    config=p.ReadPointerFromMemory(ctx+0xa0,e)
    print('config policy='+binascii.hexlify(p.ReadMemory(config+0x230,48,e)))
    state=p.ReadPointerFromMemory(ctx+0x88,e)
    print('frontend state header='+binascii.hexlify(p.ReadMemory(state,16,e)))
    # Both matched frontends place this shared state at native context+0x810.
    selector=0xc35c if os.environ.get('PVG_NATIVE_PROGRAM_AUTO') else 0xc36c
    print('frontend submit selector=%d' % p.ReadUnsignedFromMemory(state-0x810+selector,1,e))
    if name in ('gldUpdateDispatch','PVGGLProgramDispatch'):
        dispatch=frame.FindRegister('rdx').GetValueAsUnsigned()
        print('dispatch bytes='+binascii.hexlify(p.ReadMemory(dispatch,32,e)))
        if os.environ.get('PVG_LEGACY_REFERENCE_POLICY'):
            # Diagnostic A/B only: values observed for this immediate-mode
            # triangle in the reference. NOT a general policy adapter.
            pair=os.environ['PVG_LEGACY_REFERENCE_POLICY']
            if pair=='state':
                # Research only. This does not classify the later repacked
                # fallback path; mixed-path tests must expose that gap.
                native=state-0x810
                submit=p.ReadUnsignedFromMemory(native+selector,1,e)
                kind=p.ReadUnsignedFromMemory(native+0xa3e8,2,e)
                if submit==0:values=[1,1]
                elif submit==255:values=[{0x1401:2,0x1403:4,0x1405:8,0x14ff:1}.get(kind,0),4]
                else:
                    print('unclassified submit selector=%d' % submit);return True
                print('research state policy=%s index=0x%x' % (values,kind))
            else:values=[1,1] if pair=='1' else [int(x) for x in pair.split(',')]
            if len(values)!=2 or any(x not in (1,2,4,8) for x in values):return True
            p.WriteMemory(state+12,struct.pack('BB',*values),e)
            flags=p.ReadUnsignedFromMemory(dispatch+4,4,e)
            p.WriteMemory(dispatch+4,struct.pack('<I',flags|0x06000000),e)
            print('research reference policy applied ok=%s' % e.Success())
    if 'VertexArrayDescriptor' in name:
        obj=frame.FindRegister('rsi').GetValueAsUnsigned()
        desc=p.ReadPointerFromMemory(obj+0x18,e)
        table=p.ReadPointerFromMemory(desc+0x68,e)
        n=p.ReadUnsignedFromMemory(table,4,e)
        print('reflection count=%d data=%s' % (n,binascii.hexlify(p.ReadMemory(table,min(8+n*8,256),e))))
        bp=p.GetTarget().BreakpointCreateByAddress(frame.GetThread().GetFrameAtIndex(1).GetPC())
        bp.SetOneShot(True);bp.SetThreadID(frame.GetThread().GetThreadID())
        bp.SetScriptCallbackFunction(__name__+'.returned');returns[bp.GetID()]=ctx
    return False
def __lldb_init_module(debugger,internal_dict):
    for name in ('GLDContextRec::buildPrimitiveBufferVertexArrayDescriptor(GLDPipelineProgramRec*)',
                 'GLDContextRec::buildVertexArrayDescriptor(GLDPipelineProgramRec*, GLDVertexArrayRec*)',
                 'PVGGLProgramDispatch' if os.environ.get('PVG_NATIVE_PROGRAM_AUTO') else 'gldUpdateDispatch',
                 'gldBeginPrimitiveBuffer'):
        bp=debugger.GetSelectedTarget().BreakpointCreateByName(name)
        bp.SetScriptCallbackFunction(__name__+'.enter')
