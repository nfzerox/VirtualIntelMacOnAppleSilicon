"""LLDB read-only UART snapshot from a stopped, source-built QEMU process.

Import with `command script import PATH`, then `vim-serial-snapshot`. No target
expression/function call, UART register read, FIFO consumption or memory write.
The debugger's actual DWARF types supply all offsets. Detach after the snapshot.
"""
import json
import lldb


def snapshot(debugger, command, result, internal_dict):
    target=debugger.GetSelectedTarget()
    process=target.GetProcess()
    if process.GetState()!=lldb.eStateStopped:
        result.SetError('Stop the owned QEMU process before taking a consistent snapshot');return
    bus=target.FindFirstGlobalVariable('isabus')
    serial_type=target.FindFirstType('ISASerialState')
    object_type=target.FindFirstType('Object')
    if not bus.IsValid() or not serial_type.IsValid() or not object_type.IsValid():
        result.SetError('Matching QEMU DWARF types/global ISA bus unavailable');return
    child=bus.Dereference().GetChildMemberWithName('parent_obj').GetChildMemberWithName('children').GetChildMemberWithName('tqh_first')
    seen=set();rows=[]
    while child.IsValid() and child.GetValueAsUnsigned():
        address=child.GetValueAsUnsigned()
        if address in seen or len(seen)>=128:
            result.SetError('Invalid bounded ISA child list');return
        seen.add(address)
        node=child.Dereference()
        device=node.GetChildMemberWithName('child')
        obj=device.Cast(object_type.GetPointerType()).Dereference()
        name=obj.GetChildMemberWithName('class').Dereference().GetChildMemberWithName('type').Dereference().GetChildMemberWithName('name')
        error=lldb.SBError()
        typename=process.ReadCStringFromMemory(name.GetValueAsUnsigned(),128,error)
        if not error.Success():
            result.SetError('Cannot read the ISA device type identity');return
        if typename=='isa-serial':
            serial=device.Cast(serial_type.GetPointerType()).Dereference()
            state=serial.GetChildMemberWithName('state')
            row={name:serial.GetChildMemberWithName(name).GetValueAsUnsigned() for name in ('index','iobase','isairq')}
            row.update({name:state.GetChildMemberWithName(name).GetValueAsUnsigned() for name in
                        ('divider','ier','iir','lcr','mcr','lsr','msr','fcr','thr_ipending','timeout_ipending','recv_fifo_itl')})
            for name in ('recv_fifo','xmit_fifo'):
                fifo=state.GetChildMemberWithName(name)
                row[name]={field:fifo.GetChildMemberWithName(field).GetValueAsUnsigned() for field in ('capacity','head','num')}
            rows.append(row)
        child=node.GetChildMemberWithName('sibling').GetChildMemberWithName('tqe_next')
    result.AppendMessage(json.dumps({'UARTs':rows,'TargetExecution':False,'RegisterSideEffects':False},indent=2))


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand('command script add -f qemu_serial_snapshot.snapshot vim-serial-snapshot')
