"""Read-only LLDB trace of legacy descriptors before donor consumption."""
import lldb
import binascii

hits=0
def generator(frame,loc,unused):
    print('legacy generator entered: '+str(frame.GetFunctionName()))
    return False

def load(frame,loc,unused):
    global hits
    hits+=1
    if hits>6:return False
    process=frame.GetThread().GetProcess()
    error=lldb.SBError()
    ctx=frame.FindRegister('rdi').GetValueAsUnsigned()
    objects=process.ReadPointerFromMemory(ctx+0x90,error)
    for stage in (0,4):
        obj=process.ReadPointerFromMemory(objects+0x400+stage*8,error)
        if not obj:continue
        desc=process.ReadPointerFromMemory(obj+0x18,error)
        data=process.ReadMemory(desc,0xa0,error)
        print('legacy load=%d stage=%d object=0x%x desc=0x%x bytes=%s' %
              (hits,stage,obj,desc,binascii.hexlify(data)))
    return False

def __lldb_init_module(debugger,internal_dict):
    bp=debugger.GetSelectedTarget().BreakpointCreateByName(
        'GLDContextRec::loadCurrentPipelinePrograms(unsigned int*)')
    bp.SetScriptCallbackFunction(__name__+'.load')
    for name in ('gleCallVertexStateToLLVMIR','gleCallFragmentStateToLLVMIR',
                 'glpVertexStateToPPStream','glpFragmentStateToPPStream'):
        bp=debugger.GetSelectedTarget().BreakpointCreateByName(name)
        bp.SetScriptCallbackFunction(__name__+'.generator')
