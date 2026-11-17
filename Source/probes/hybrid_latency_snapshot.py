"""Read the optional native-host slow-operation rings, with ALL threads stopped.

Enable VIM_HYBRID_LATENCY_TRACE=1 only for a component diagnostic launch. No
periodic tally, boundary requests or profiler traps are required. This also
enables the existing inert MMU census (observe, never skip stores). Then attach
OUTSIDE the timed workload, import this file, run:
    vim-hybrid-latency /absolute/new-result.json
Explicitly `process detach` before continuing. A fallback-away interval includes
TCG execution, host work and descheduling; it is NOT pure transition overhead.
HLT idle waits are excluded; this is not a wakeup diagnostic. Ring overwrite is reported;
absence of a retained event is never proof a long operation did not occur.
"""
import ctypes
import json
from pathlib import Path
import shlex
import time
import lldb


def number(value):
    error = lldb.SBError()
    result = value.GetValueAsUnsigned(error)
    if not value.IsValid() or error.Fail():
        raise ValueError('Unreadable diagnostic field: ' + str(value.GetName()))
    return result


def snapshot(debugger, command, result, internal_dict):
    del internal_dict
    try:
        args = shlex.split(command)
        if len(args) != 1 or not Path(args[0]).is_absolute():
            raise ValueError('usage: vim-hybrid-latency /absolute/new-result.json')
        target = debugger.GetSelectedTarget()
        process = target.GetProcess()
        if process.GetState() != lldb.eStateStopped:
            raise ValueError('Stop all threads before reading these single-writer rings.')
        matches = target.GetModuleAtIndex(0).FindGlobalVariables(target, 'engine_latency_rings', 2)
        if len(matches) != 1:
            raise ValueError('Diagnostic rings not uniquely available in the running component.')
        rings = matches[0]
        if rings.GetNumChildren() != 8:
            raise ValueError('Unknown ring ABI.')
        class Timebase(ctypes.Structure):
            _fields_ = [('numer', ctypes.c_uint32), ('denom', ctypes.c_uint32)]
        base = Timebase()
        native = ctypes.CDLL('/usr/lib/libSystem.B.dylib')
        status = native.mach_timebase_info(ctypes.byref(base))
        if status or not base.numer or not base.denom:
            raise ValueError('Native debugger timebase unavailable.')
        scale = base.numer / base.denom / 1e9
        cpus = []
        for cpu in range(8):
            ring = rings.GetChildAtIndex(cpu)
            total = number(ring.GetChildMemberWithName('total'))
            slots = ring.GetChildMemberWithName('samples')
            capacity = slots.GetNumChildren()
            if capacity != 128:
                raise ValueError('Unknown ring capacity.')
            events = []
            for sequence in range(max(1, total-capacity+1), total+1):
                value = slots.GetChildAtIndex((sequence-1) % capacity)
                fields = {key: number(value.GetChildMemberWithName(key)) for key in
                    ('sequence', 'start_ticks', 'duration_ticks', 'wait_ticks', 'kind', 'code', 'view', 'cpl')}
                if fields['sequence'] != sequence:
                    raise ValueError('Incomplete record; was every thread stopped?')
                detail = value.GetChildMemberWithName('detail')
                fields.update(Detail=[hex(number(detail.GetChildAtIndex(i))) for i in range(2)],
                    StartSeconds=fields['start_ticks']*scale,
                    DurationMS=fields['duration_ticks']*scale*1000,
                    TranslatorWaitMS=fields['wait_ticks']*scale*1000)
                events.append(fields)
            cpus.append(dict(CPU=cpu, TotalRecorded=total,
                Overwritten=max(0, total-capacity), Events=events))
        native.mach_absolute_time.restype = ctypes.c_uint64
        data = dict(PID=process.GetProcessID(), ObservedAt=time.time(),
            ObservedMachTicks=native.mach_absolute_time(),
            MachTimebase=dict(numer=base.numer, denom=base.denom), CPUs=cpus,
            Caution='Stopped-process snapshot outside workload. Away includes TCG/host/descheduling; HLT idle excluded; only newest slow events retained.')
        with Path(args[0]).open('x') as output:
            json.dump(data, output, indent=2)
            output.write('\n')
        result.AppendMessage('Saved bounded slow operations; explicitly detach before continuing.')
    except Exception as error:
        result.SetError(str(error))


def __lldb_init_module(debugger, internal_dict):
    del internal_dict
    debugger.HandleCommand('command script add -f ' + __name__ + '.snapshot vim-hybrid-latency')
