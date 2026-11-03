"""Host-side write watch for an explicitly selected TCG guest RAM word.

Attach LLDB to the development QEMU process, import this script, then invoke
vim-watch-ram ADDRESS and continue. Stops on eight 01 bytes or after 64 writes.
Uses debug metadata and read-only debugger reads, never inferior expressions.
This also catches DMA and guest virtual aliases that a guest watch misses.
"""
import lldb
import json

_counts = {}


def changed(frame, watchpoint, _internal):
    process = frame.GetThread().GetProcess()
    error = lldb.SBError()
    data = process.ReadMemory(watchpoint.GetWatchAddress(), 8, error)
    count = _counts.get(watchpoint.GetID(), 0) + 1
    _counts[watchpoint.GetID()] = count
    print(f'RAM write {count}: {data.hex() if error.Success() else error.GetCString()} '
          f'host PC={frame.GetPC():#x}', flush=True)
    # QEMU's x86-64 TCG backend reserves RBP for CPUArchState (TCG_AREG0).
    # Read its actual DWARF layout; do not guess offsets or execute code in it.
    if count == 2 or data == b'\x01'*8:
        target=process.GetTarget()
        env_address=frame.FindRegister('rbp').GetValueAsUnsigned()
        env=target.CreateValueFromAddress('guest_env',lldb.SBAddress(env_address,target),target.FindFirstType('CPUArchState'))
        regs=env.GetChildMemberWithName('regs')
        print(json.dumps({'GuestEIP':hex(env.GetChildMemberWithName('eip').GetValueAsUnsigned()),
            'GuestRegs':[hex(regs.GetChildAtIndex(i).GetValueAsUnsigned()) for i in range(16)],
            'Env':hex(env_address), 'HostR13':frame.FindRegister('r13').GetValue(),
            'HostR14':frame.FindRegister('r14').GetValue()}),flush=True)
    return error.Fail() or data == b'\x01'*8 or count >= 64


def install(debugger, command, result, _internal):
    try:
        address = int(command.strip(), 0)
        target = debugger.GetSelectedTarget()
        block = target.FindFirstGlobalVariable('ram_list').GetChildMemberWithName('blocks').GetChildMemberWithName('lh_first')
        for _ in range(64):
            if not block.IsValid() or not block.GetValueAsUnsigned():
                raise RuntimeError('pvg-ram RAMBlock not found')
            value = block.Dereference()
            name = value.GetChildMemberWithName('idstr').GetSummary() or ''
            if name == '"pvg-ram"':
                size = value.GetChildMemberWithName('used_length').GetValueAsUnsigned()
                base = value.GetChildMemberWithName('host').GetValueAsUnsigned()
                if address < 0 or address+8 > size or not base:
                    raise ValueError('requested word is outside pvg-ram')
                error = lldb.SBError()
                before=target.GetProcess().ReadMemory(base+address,8,error)
                if error.Fail() or before == b'\x01'*8:
                    raise RuntimeError('word is already corrupted or unreadable; start a new diagnostic boot')
                watch = target.WatchAddress(base+address, 8, False, True, error)
                if error.Fail():
                    raise RuntimeError(error.GetCString())
                debugger.HandleCommand('watchpoint command add -F '+__name__+'.changed '+str(watch.GetID()))
                result.AppendMessage(f'Guest RAM {address:#x} -> host {base+address:#x}, before {before.hex()}, watch {watch.GetID()}')
                return
            block = value.GetChildMemberWithName('next').GetChildMemberWithName('le_next')
        raise RuntimeError('RAMBlock list exceeded diagnostic bound')
    except Exception as error:
        result.SetError(str(error))


def __lldb_init_module(debugger, _internal):
    debugger.HandleCommand('command script add -f '+__name__+'.install vim-watch-ram')
