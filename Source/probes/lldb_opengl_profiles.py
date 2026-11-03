"""Read-only owned-fixture GLI profile trace for physical Mavericks 13F1911.
Stop at VIMProfileBeforeCreate, import, vim-profile-trace, continue. Never
attach this to WindowServer. Exact native module UUID/entries, 8 stops each.
"""
import lldb
import struct
from lldb_legacy_private_integer import reg, read

entries={}
returns={}
counts={}
breakpoints=[]
log_path=None
pointer_format="<Q"
record_format="<Q12I"
stack_register="rsp"
result_register="rax"


def log(line):
    print(line)
    if log_path:
        with open(log_path,"a") as stream:stream.write(line+"\n")


def callback(debugger,bp):
    debugger.HandleCommand('breakpoint command add -s command -o "script '+__name__+'.event()" '+str(bp.GetID()))


def record(process,address):
    return repr(read(process,address,record_format)) if address else "none"


def event():
    debugger=lldb.debugger;target=debugger.GetSelectedTarget();process=target.GetProcess()
    frame=process.GetSelectedThread().GetFrameAtIndex(0)
    try:
        pc=frame.GetPC();tid=frame.GetThread().GetThreadID();key=(pc,tid)
        if key in returns:
            kind,out=returns.pop(key);error=reg(frame,result_register)&0xffffffff
            value=read(process,out,pointer_format)[0] if not error and out else 0
            log("PROFILE_RETURN %s error=%d output=%x record=%s" %
                  (kind,error,value,record(process,value) if kind=="choose" and value else "not-format"))
        else:
            kind,bp=entries[pc];counts[kind]=counts.get(kind,0)+1
            if counts[kind]>=8:bp.SetEnabled(False)
            args=read(process,reg(frame,"esp")+4,"<6I") if pointer_format=="<I" else tuple(
                reg(frame,r) for r in ("rdi","rsi","rdx","rcx","r8","r9"))
            out=args[0]
            if kind=="choose":
                address=args[1];attributes=[]
                # Native CGL's normalized attribute stream is integers. Log
                # through its terminator, never beyond a fixed bounded prefix.
                for i in range(64):
                    value=read(process,address+i*4,"<i")[0];attributes.append(value)
                    if not value:break
                log("PROFILE_ENTRY choose attrs="+repr(attributes))
            else:
                log("PROFILE_ENTRY create flags=%x share=%x format=%s" %
                      (args[5],args[2],record(process,args[1])))
            ret=read(process,reg(frame,stack_register),pointer_format)[0]
            bp=target.BreakpointCreateByAddress(ret);bp.SetThreadID(tid);bp.SetOneShot(True)
            returns[(ret,tid)]=(kind,out);breakpoints.append(bp);callback(debugger,bp)
    except Exception as error:
        log("PROFILE unavailable: "+str(error))
    finally:
        debugger.HandleCommand("continue")


def install(debugger,command,result,_):
    global entries,returns,counts,breakpoints,log_path,pointer_format,record_format,stack_register,result_register
    target=debugger.GetSelectedTarget()
    if target.GetExecutable().GetFilename() not in ("opengl-core-profile","opengl-core-profile-i386"):
        result.PutCString("REFUSED: use the owned opengl-core-profile executable");return
    for bp in breakpoints:target.BreakpointDelete(bp.GetID())
    entries={};returns={};counts={};breakpoints=[]
    modules=[target.GetModuleAtIndex(i) for i in range(target.GetNumModules())
             if target.GetModuleAtIndex(i).GetFileSpec().GetFilename()=="GLEngine"]
    known={"30643C97-7EBB-36EE-BEEA-13844EA0F680":(8,0x10b1,0x1204),
           "493574B0-B37C-3122-B50E-F935BEC4C6B5":(4,0x1492,0x15aa)}
    if len(modules)!=1 or modules[0].GetUUIDString().upper() not in known:
        result.PutCString("REFUSED: stop after loading native 13F1911 GLEngine");return
    width,choose,create=known[modules[0].GetUUIDString().upper()]
    pointer_format="<I" if width==4 else "<Q";record_format=pointer_format+"12I"
    stack_register="esp" if width==4 else "rsp";result_register="eax" if width==4 else "rax"
    log_path=command.strip() or None
    for kind,offset in (("choose",choose),("create",create)):
        address=modules[0].ResolveFileAddress(offset).GetLoadAddress(target)
        bp=target.BreakpointCreateByAddress(address);entries[address]=(kind,bp);breakpoints.append(bp);callback(debugger,bp)
    result.PutCString("Bounded native profile trace ready; continue the owned fixture.")


def __lldb_init_module(debugger,_):
    debugger.HandleCommand("command script add -f "+__name__+".install vim-profile-trace")
