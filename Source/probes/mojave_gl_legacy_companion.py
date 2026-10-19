"""Single-context research: supply native PP alongside native LLVM output.
Use with mojave_gl_legacy_mode (mode 2 emits modern AIR), never in desktop
applications. Attachment is deliberately late for diagnosis; production must
integrate before native parameter remapping and cover cache/lifetime changes.
"""
import lldb
import struct
companions={}

def generate(frame,loc,unused):
    target=frame.GetThread().GetProcess().GetTarget()
    vertex='Vertex' in frame.GetFunctionName()
    name='gleVertexStateToPPStream' if vertex else 'gleFragmentStateToPPStream'
    matches=target.FindFunctions(name)
    if matches.GetSize()!=1:
        print('refuse ambiguous PP generator '+name);return True
    address=matches.GetContextAtIndex(0).GetSymbol().GetStartAddress().GetLoadAddress(target)
    key=frame.FindRegister('rdi').GetValueAsUnsigned()
    expr='(void *)({ unsigned long a=0,b=0; unsigned c=0; ((void *(*)(void *,void *,void *,void *))0x%x)((void *)0x%x,&a,&b,&c); })' % (address,key)
    result=frame.EvaluateExpression(expr)
    pp=result.GetValueAsUnsigned()
    if result.GetError().Fail() or not pp:
        print('PP generation failed: '+str(result.GetError()));return True
    stage=0 if vertex else 4
    if stage in companions:return True
    companions[stage]=pp
    print('native PP companion stage=%d pointer=0x%x' % (stage,pp))
    return False

def attach(frame,loc,unused):
    process=frame.GetThread().GetProcess()
    error=lldb.SBError()
    ctx=frame.FindRegister('rdi').GetValueAsUnsigned()
    objects=process.ReadPointerFromMemory(ctx+0x90,error)
    for stage in list(companions):
        obj=process.ReadPointerFromMemory(objects+0x400+stage*8,error)
        if not obj:return True
        desc=process.ReadPointerFromMemory(obj+0x18,error)
        old=process.ReadPointerFromMemory(desc+8,error)
        ir=process.ReadPointerFromMemory(desc+0x10,error)
        if old or not ir or not error.Success():return True
        process.WriteMemory(desc+8,struct.pack('<Q',companions.pop(stage)),error)
        print('attached PP to native descriptor stage=%d ok=%s' % (stage,error.Success()))
        if not error.Success():return True
    return False

def __lldb_init_module(debugger,internal_dict):
    target=debugger.GetSelectedTarget()
    for name in ('gleCallVertexStateToLLVMIR','gleCallFragmentStateToLLVMIR'):
        bp=target.BreakpointCreateByName(name)
        bp.SetScriptCallbackFunction(__name__+'.generate')
    bp=target.BreakpointCreateByName('GLDContextRec::loadCurrentPipelinePrograms(unsigned int*)')
    bp.SetScriptCallbackFunction(__name__+'.attach')
