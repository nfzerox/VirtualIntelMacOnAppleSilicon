#!/usr/sbin/dtrace -s
/* Usage: sudo dtrace -q -s probes/tcg-tlb-sources.d -p PID
 * Counts, not a speed benchmark: pid-provider traps perturb execution.
 * Repeated CR3 compares successive observed writes on one host thread;
 * it does not prove that a flush can safely be omitted.
 */
#pragma D option quiet
#pragma D option dynvarsize=8m

BEGIN
{
    seen[0] = 0;
    last[0] = 0;
    flushes[0] = 0;
}

pid$target::cpu_x86_update_cr3:entry
{
    @events["CR3 writes"] = count();
    @cr3_repeat[seen[tid] ? (last[tid] == arg1 ? "same" : "different") : "first"] = count();
    seen[tid] = 1;
    last[tid] = arg1;
}

pid$target::cpu_x86_update_cr0:entry,
pid$target::cpu_x86_update_cr4:entry,
pid$target::helper_flush_page:entry,
pid$target::tlb_flush:entry,
pid$target::tlb_flush_by_mmuidx:entry,
pid$target::tlb_flush_one_mmuidx_locked:entry
{
    @events[probefunc] = count();
}

pid$target::tlb_flush:entry
/(++flushes[tid] & 1023) == 1/
{
    @flush_callers[ustack(8)] = count();
}

tick-15s
{
    exit(0);
}
