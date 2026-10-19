/* Guest-only, read-only five-second syscall-size/latency diagnostic.
 * Usage: sudo dtrace -q -s verification-reads.d SYSPOLICYD_PID
 * Start a separate spctl assessment first. No file names or contents collected.
 * Wall-clock syscall latency includes blocking and scheduling, not just CPU.
 */
syscall::read:entry
/pid == $1/
{
    self->read_started = timestamp;
    @requested_sizes[arg2] = count();
}

syscall::read:return
/pid == $1 && self->read_started/
{
    @latency_microseconds = quantize((timestamp - self->read_started) / 1000);
    @completed_reads = count();
    @returned_bytes = sum((int64_t)arg0 > 0 ? arg0 : 0);
    self->read_started = 0;
}

tick-5sec
{
    exit(0);
}
