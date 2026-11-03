/* Finite HOST attribution for immutable surface sealing. Inclusive times
 * overlap: do not sum the outer snapshot with its native subcalls. A snapshot
 * can also drain earlier native GL work. No Apple offsets or payload reads.
 * Run against the actual QEMU PID, then drive the real guest Finder after
 * READY. This probe exits itself; it is not a production sampling service. */
#pragma D option quiet
#pragma D option dynvarsize=16m
BEGIN {
    began=timestamp;self->snapshot=(uint64_t)0;self->flush=(uint64_t)0;
    printf("READY host snapshot attribution target=%d\n",$target);
}
/* Some public GL exports tail-jump to the dispatch table and consequently
 * have no pid-provider return probe. Flush -> final GetError is a separately
 * labelled call-site span, not a made-up zero-duration return measurement. */
pid$target::glFlush:entry
/self->snapshot/ { self->flush=timestamp; }
pid$target::glGetError:entry
/self->snapshot && self->flush/
{
    this->us=(timestamp-self->flush)/1000;
    @flush_calls=count();@flush_us=sum(this->us);@flush_max=max(this->us);
    self->flush=0;
}
pid$target:libVIMPassthroughOpenGLHost.dylib:DGSurfaceSnapshotCreateCommands:entry
{ self->snapshot=timestamp;@entries["snapshot"]=count(); }
pid$target:libVIMPassthroughOpenGLHost.dylib:DGSurfaceSnapshotCreateCommands:return
/self->snapshot/
{
    this->us=(timestamp-self->snapshot)/1000;
    @calls["snapshot"]=count();@us["snapshot"]=sum(this->us);@max["snapshot"]=max(this->us);
    @latency=quantize(this->us);self->snapshot=0;self->flush=0;
}
pid$target::glFlush:entry,
pid$target::glBlitFramebufferEXT:entry,
pid$target::glFramebufferTexture2DEXT:entry,
pid$target::glCheckFramebufferStatusEXT:entry,
pid$target::glGetIntegerv:entry,
pid$target::glGetError:entry,
pid$target::glWaitSync:entry,
pid$target::IOSurfaceCreate:entry,
pid$target::CGLTexImageIOSurface2D:entry,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGSnapshotPoolTake:entry,
pid$target::pthread_mutex_lock:entry
/self->snapshot/
{ self->at[probefunc]=timestamp;@entries[probefunc]=count(); }
pid$target::glFlush:return,
pid$target::glBlitFramebufferEXT:return,
pid$target::glFramebufferTexture2DEXT:return,
pid$target::glCheckFramebufferStatusEXT:return,
pid$target::glGetIntegerv:return,
pid$target::glGetError:return,
pid$target::glWaitSync:return,
pid$target::IOSurfaceCreate:return,
pid$target::CGLTexImageIOSurface2D:return,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGSnapshotPoolTake:return,
pid$target::pthread_mutex_lock:return
/self->snapshot && self->at[probefunc]/
{
    this->us=(timestamp-self->at[probefunc])/1000;
    @calls[probefunc]=count();@us[probefunc]=sum(this->us);@max[probefunc]=max(this->us);
    self->at[probefunc]=0;
}
tick-1s /timestamp-began>=12000000000/ { exit(0); }
ERROR { printf("INVALID host snapshot attribution error\n");exit(2); }
END {
    printf("function entries returns inclusive_us maximum_us\n");
    printa("%s %@d %@d %@d %@d\n",@entries,@calls,@us,@max);
    printf("flush-entry to final-error-query span calls inclusive_us maximum_us\n");
    printa("%@d %@d %@d\n",@flush_calls,@flush_us,@flush_max);
    printa(@latency);
}
