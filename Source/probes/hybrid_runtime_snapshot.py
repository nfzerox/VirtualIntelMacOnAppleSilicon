"""Finite, read-only LLDB snapshot of the hybrid engine's C counters.

Attach outside the workload being timed. No expressions, injected calls, target
writes, breakpoints or recurring sampling. Explicitly detach after the command:

  lldb -b -p PID \
    -o 'command script import /absolute/path/hybrid_runtime_snapshot.py' \
    -o 'vim-hybrid-snapshot /absolute/new-snapshot.json' \
    -o 'process detach' -o quit

Compare snapshots from the same emulator session. Attaching perturbs scheduling;
counter deltas bracket a workload, but are not an unprofiled latency benchmark.
"""
import ctypes
import json
from pathlib import Path
import shlex
import time

import lldb


# Existing engine_abi.h ABI: EngineCtx instances occupy separate 64 KiB slots.
CTX_SLOT = 64 << 10
TRANSLATOR_FIELDS = (
    'blocks', 'insns', 'words', 'translate_ns', 'invalidated', 'killed',
    'killed_unmoved', 'kill_calls', 'reused', 'again_live', 'again_dead',
    'shared_blocks', 'shared_found', 'i386_blocks', 'i386_again',
    'hash_refills', 'hash_forgets', 'cache_resets', 'cache_private_resets',
    'invalidation_membership_enabled', 'invalidation_membership_reliable',
    'invalidation_membership_inserts', 'invalidation_membership_view_visits',
    'invalidation_membership_stale_visits', 'invalidation_membership_removals',
    'invalidation_membership_tracking_failures', 'invalidation_membership_full_scans',
)
CPU_FIELDS = (
    'stat_svc', 'stat_fills', 'stat_dirty', 'stat_pf_delivered', 'stat_pt_writes',
    'stat_pt_writes_zap', 'stat_pt_step', 'stat_smc', 'stat_mon_wait',
    'stat_mon_hold', 'stat_irq', 'stat_steps', 'stat_cr3', 'stat_iret',
    'stat_syscall', 'stat_mmio', 'stat_view_evictions', 'stat_pt_unshadow',
    'stat_pool_reclaim', 'stat_note_code', 'stat_note_pt', 'stat_nm', 'stat_pt_stale',
    'stat_mmu_store_observed', 'stat_mmu_store_unchanged',
    'stat_mmu_flood_decisions', 'stat_mmu_xmark_zaps',
    'keeper_inactive_guard', 'stat_keeper_inactive_suppressed',
    'stat_keeper_inactive_threshold_fallbacks',
    'profile_mmu_observations',
)


def counter(value):
    if not value.IsValid() or value.GetError().Fail():
        raise ValueError('unreadable engine counter: ' + str(value.GetName()))
    if value.GetType().IsArrayType():
        return [counter(value.GetChildAtIndex(i)) for i in range(value.GetNumChildren())]
    error = lldb.SBError()
    result = value.GetValueAsUnsigned(error)
    if error.Fail():
        raise ValueError(str(error))
    return result


def fields(value, names):
    return {name: counter(value.GetChildMemberWithName(name)) for name in names}


def snapshot(debugger, command, result, internal_dict):
    del internal_dict
    try:
        args = shlex.split(command)
        if len(args) != 1 or not Path(args[0]).is_absolute():
            raise ValueError('usage: vim-hybrid-snapshot /absolute/new-snapshot.json')
        target = debugger.GetSelectedTarget()
        process = target.GetProcess()
        if process.GetState() != lldb.eStateStopped:
            raise ValueError('attach and stop the process before reading counters')
        # Locate the translator globals in their actual compilation unit; never
        # evaluate an expression or call into the suspended emulator.
        module = target.GetModuleAtIndex(0)
        variables = {}
        for name in ('ctx_ptr', 'stats'):
            matches = [value for value in module.FindGlobalVariables(target, name, 100)
                       if value.GetDeclaration().GetFileSpec().GetFilename() == 'translate.c']
            if len(matches) != 1:
                raise ValueError('translator global not uniquely available: ' + name)
            variables[name] = matches[0]
        pointer = variables['ctx_ptr']
        address = counter(pointer)
        context_type = pointer.GetType().GetPointeeType()
        first = target.CreateValueFromAddress('cpu0', lldb.SBAddress(address, target), context_type)
        count = counter(first.GetChildMemberWithName('vcpu_count'))
        if not 1 <= count <= 8 or context_type.GetByteSize() > CTX_SLOT:
            raise ValueError('unexpected engine context layout or vCPU count')
        cpus = []
        for index in range(count):
            context = target.CreateValueFromAddress('cpu' + str(index),
                lldb.SBAddress(address + index * CTX_SLOT, target), context_type)
            cpus.append(fields(context, CPU_FIELDS))
            # Optional appended diagnostics: old qualified binaries remain readable.
            for name in ('profile_shift_helpers', 'stat_shift_helpers_unclassified', 'stat_shift_helpers',
                         'profile_x87_helpers', 'stat_x87_helpers_unclassified', 'stat_x87_helpers',
                         'tsc_counter_offset'):
                value = context.GetChildMemberWithName(name)
                if value.IsValid():
                    cpus[-1][name] = counter(value)
            ring = context.GetChildMemberWithName('note_rescans')
            if ring.IsValid():
                total = counter(ring.GetChildMemberWithName('total'))
                slots = ring.GetChildMemberWithName('samples')
                if slots.GetNumChildren() != 64:
                    raise ValueError('unknown note rescan ring size')
                samples = []
                for sequence in range(max(1, total-63), total+1):
                    row = fields(slots.GetChildAtIndex((sequence-1)%64),
                        ('sequence', 'start_ticks', 'duration_ticks', 'code_frames', 'table_frames'))
                    if row['sequence'] != sequence:
                        raise ValueError('incomplete note rescan record; stop all threads')
                    samples.append(row)
                cpus[-1]['NoteRescans'] = dict(Total=total, Overwritten=max(0,total-64),
                    ActiveStartTicks=counter(ring.GetChildMemberWithName('active_start_ticks')),
                    Samples=samples)
        # Query the debugger's own native timebase, NOT a target function.
        class Timebase(ctypes.Structure):
            _fields_ = [('numer', ctypes.c_uint32), ('denom', ctypes.c_uint32)]
        base = Timebase()
        native = ctypes.CDLL('/usr/lib/libSystem.B.dylib')
        status = native.mach_timebase_info(ctypes.byref(base))
        if status or not base.denom:
            raise ValueError('native Mach timebase unavailable')
        native.mach_absolute_time.restype = ctypes.c_uint64
        data = {'PID': process.GetProcessID(), 'ObservedAt': time.time(),
                'ObservedMachTicks': native.mach_absolute_time(),
                'MachTimebase': {'numer': base.numer, 'denom': base.denom},
                'Translator': fields(variables['stats'], TRANSLATOR_FIELDS), 'CPUs': cpus}
        for name in ('call_tails', 'jump_tails'):
            value = variables['stats'].GetChildMemberWithName(name)
            if value.IsValid():
                data['Translator'][name] = counter(value)
        with Path(args[0]).open('x') as output:
            json.dump(data, output, indent=2)
            output.write('\n')
        result.AppendMessage('Read-only snapshot saved; explicitly detach before the workload.')
    except Exception as error:
        result.SetError(str(error))


def __lldb_init_module(debugger, internal_dict):
    del internal_dict
    debugger.HandleCommand('command script add -f ' + __name__ + '.snapshot vim-hybrid-snapshot')
