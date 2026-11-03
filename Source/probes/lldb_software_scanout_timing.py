"""Arm existing owned-QEMU scanout counters, then detach before a workload.

vim-software-scanout-timing on|off|status QEMU_UUID
No inferior calls, breakpoints, pixel reads, ownership changes or new timers.
The existing aggregate reports include scheduling/ACK delay, NOT GPU time.
"""
import json
import lldb


def command(debugger, text, result, internal_dict):
    try:
        parts = text.split()
        if len(parts) != 2 or parts[0] not in ('on', 'off', 'status'):
            raise ValueError('Use vim-software-scanout-timing on|off|status QEMU_UUID')
        target = debugger.GetSelectedTarget()
        process = target.GetProcess()
        module = target.GetModuleAtIndex(0)
        expected = parts[1].replace('-', '').upper()
        if (target.GetExecutable().GetFilename() != 'qemu-system-x86_64' or
                process.GetState() != lldb.eStateStopped or
                module.GetUUIDString().replace('-', '').upper() != expected):
            raise ValueError('Require the stopped owned QEMU and its actual executable UUID')

        def field(value, *names):
            for name in names:
                if value.GetType().IsPointerType():
                    value = value.Dereference()
                value = value.GetChildMemberWithName(name)
                if not value.IsValid() or value.GetError().Fail():
                    raise ValueError('Cannot read typed scanout field: ' + name)
            return value

        ds = target.FindFirstGlobalVariable('display_state')
        listener = field(ds, 'listeners', 'lh_first')
        found = []
        for _ in range(16):
            pointer = listener.GetValueAsUnsigned()
            if not pointer:
                break
            error = lldb.SBError()
            name = process.ReadCStringFromMemory(
                field(listener, 'ops', 'dpy_name').GetValueAsUnsigned(), 64, error)
            if error.Fail():
                raise ValueError('Cannot read the listener identity')
            if name == 'virtual-intel-mac-installer':
                kind = target.FindFirstType('VIMDisplay')
                if not kind.IsValid() or kind.GetFieldAtIndex(0).GetName() != 'dcl' or kind.GetFieldAtIndex(0).GetOffsetInBytes():
                    raise ValueError('Unexpected VIMDisplay layout')
                found.append(target.CreateValueFromAddress(
                    'software_scanout', lldb.SBAddress(pointer, target), kind))
            listener = field(listener, 'next', 'le_next')
        if len(found) != 1:
            raise ValueError('Require one software scanout, not a PVG or arbitrary listener')
        scanout = found[0]
        flag = field(scanout, 'timing')
        if flag.GetType().GetByteSize() != 1 or flag.GetValueAsUnsigned() not in (0, 1):
            raise ValueError('Unexpected timing flag')
        if parts[0] != 'status':
            error = lldb.SBError()
            wanted = bytes([int(parts[0] == 'on')])
            if process.WriteMemory(flag.GetLoadAddress(), wanted, error) != 1 or error.Fail():
                raise ValueError('Timing selection write failed')
            if process.ReadMemory(flag.GetLoadAddress(), 1, error) != wanted or error.Fail():
                raise ValueError('Timing selection readback failed')
        report = {name: field(scanout, name).GetValueAsUnsigned() for name in
                  ('timing', 'event_driven', 'sequence', 'busy', 'dirty', 'frames', 'acks')}
        report['Caution'] = 'Sequence counts submitted frames, not displayed FPS. Existing counters reset per aggregate report.'
        result.PutCString(json.dumps(report, sort_keys=True))
    except (ValueError, AttributeError) as error:
        result.SetError(str(error))


def __lldb_init_module(debugger, internal_dict):
    debugger.HandleCommand('command script add -f ' + __name__ + '.command vim-software-scanout-timing')
