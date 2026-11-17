/* Read-only, finite guest-kernel diagnostic, not a workaround.
 * dtrace -qs compressed-page-verification.d FILE_OFFSET SECONDS
 * Run only after native logs identify the failing 16KiB verification offset.
 * Trace APFS's actual VFS callback, not an optionally inlined VNOP dispatcher.
 * The prefix below is the published LP64 struct vnop_verify_args contract.
 * Native APFS exposes no CTF argument types. Select the exact offset from
 * the native kernel error, not execname: asynchronous page-ins can be
 * verified on a kernel worker rather than the process whose read fails.
 * Speculate the 16KiB block before verification, discard successful reads,
 * and retain the first observed failure's before/after bytes for offline SHA.
 * No payload writes, integrity bypasses, credential reads or continuous tracing.
 */
#pragma D option quiet
#pragma D option bufsize=256k
#pragma D option specsize=128k

struct vim_verify_args {
    uintptr_t descriptor;
    uintptr_t vnode;
    int64_t offset;
    uintptr_t buffer;
    uint64_t size;
};

dtrace:::BEGIN
{
    started = timestamp;
    captured = 0;
    observed = 0;
    printf("Observing verification offset %llu for %d seconds\n", (unsigned long long)$1, (int)$2);
}

fbt:com.apple.filesystems.apfs:apfs_vnop_verify:entry
/arg0 != 0 && ((struct vim_verify_args *)arg0)->buffer != 0 &&
 ((struct vim_verify_args *)arg0)->size == 16384 &&
 ((struct vim_verify_args *)arg0)->offset == $1 && captured == 0/
{
    observed++;
    self->buffer = ((struct vim_verify_args *)arg0)->buffer;
    self->vnode = ((struct vim_verify_args *)arg0)->vnode;
    self->spec = speculation();
    speculate(self->spec);
    printf("VERIFY before pid=%d name=%s tid=%d vnode=%p\n",
        pid, execname, tid, (void *)self->vnode);
    tracemem((void *)self->buffer, 16384);
}

fbt:com.apple.filesystems.apfs:apfs_vnop_verify:return
/self->buffer != 0 && self->spec != 0 && arg1 != 0 && captured == 0/
{
    speculate(self->spec);
    printf("VERIFY failure pid=%d name=%s tid=%d vnode=%p status=%d\n",
        pid, execname, tid, (void *)self->vnode, (int)arg1);
    printf("VERIFY after\n");
    tracemem((void *)self->buffer, 16384);
}

fbt:com.apple.filesystems.apfs:apfs_vnop_verify:return
/self->buffer != 0 && self->spec != 0 && arg1 != 0 && captured == 0/
{ commit(self->spec); self->spec = 0; captured++; }

fbt:com.apple.filesystems.apfs:apfs_vnop_verify:return
/self->buffer != 0 && self->spec != 0/
{ discard(self->spec); self->spec = 0; }

fbt:com.apple.filesystems.apfs:apfs_vnop_verify:return
/self->buffer != 0/
{ self->buffer = 0; }

profile:::tick-1s
/timestamp - started >= (uint64_t)$2 * 1000000000/
{
    exit(0);
}

dtrace:::END
{
    printf("Observed %d matching calls; captured %d failures\n", (int)observed, (int)captured);
}
