/* Executor x64 FFFA8250-BA1F-356A-9BE7-C7F7C524EFBD only.
 * Exact disassembly: Subscribe+19c CALL dispatch_async_f has job in RSI;
 * Notify entry receives it in RDI. Match opaque identity WITHOUT dereferencing
 * process memory. No Apple layouts, guest pointers, pixels, host debugger,
 * GL/scheduler/policy writes or polling. Queue spans are not GPU time.
 * Worker span includes actual completion/materialization AND callback delivery.
 */
#pragma D option quiet
#pragma D option dynvarsize=4m
#pragma D option aggsize=4m
BEGIN {began=timestamp;budget=2048;printf("READY host backing notifications target=%d\n",$target);}
pid$target:libVIMPassthroughOpenGLHost.dylib:DGBackingTicketSubscribe:19c
{this->job=(uint64_t)uregs[R_RSI];submitted[this->job]=timestamp;job_id[this->job]=++serial;@subscriptions=count();}
pid$target:libVIMPassthroughOpenGLHost.dylib:DGBackingTicketNotify:entry
{
    this->job=(uint64_t)uregs[R_RDI];self->ticket=job_id[this->job];self->work=timestamp;
    this->at=submitted[this->job];
    if(this->at && budget>0){
        @queued[(this->at-began)/1000,self->ticket,(timestamp-this->at)/1000]=count();budget--;
    }
    @begins=count();@unmatched[this->at==0]=count();submitted[this->job]=0;job_id[this->job]=0;
}
pid$target:libVIMPassthroughOpenGLHost.dylib:DGBackingTicketNotify:return /self->work/
{
    if(budget>0){@workers[(self->work-began)/1000,self->ticket,(timestamp-self->work)/1000]=count();budget--;}
    @ends=count();self->ticket=0;self->work=0;
}
tick-1s /timestamp-began>=24000000000/ {exit(0);}
ERROR {printf("INVALID host backing notifications: probe error\n");exit(2);}
END {
    printf("QUEUED enqueue_us diagnostic_job_id enqueue_to_worker_us count\n");printa(@queued);
    printf("WORKERS start_us diagnostic_job_id body_before_free_us count\n");printa(@workers);
    printf("BALANCE enqueues/worker-begins/ends/unmatched\n");
    printa(@subscriptions);printa(@begins);printa(@ends);printa(@unmatched);
    printf("END host backing notifications budget=%d\n",budget);
}
