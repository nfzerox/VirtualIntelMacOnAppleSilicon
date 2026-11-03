/* Exact Intel QEMU UUID5EF0FD8F-B497-31A8-BD08-70C5C551AC32 ONLY.
 * Disassembly: wait_begin+d2 is its actual RET after bql_unlock;
 * wait_end+22 is immediately after the CALL bql_lock_impl. This binary
 * also INLINES begin in exchange+1dc and scatter+48d, both immediately after
 * bql_unlock; omitting these loses most native-span coverage. Verify all
 * instructions/loaded UUID before attaching. No pointers, guest packets,
 * private Apple data, host ptrace or rendering-policy changes.
 * All timings include this finite instrumentation, NOT clean FPS/GPU time.
 */
#pragma D option quiet
#pragma D option dynvarsize=4m
#pragma D option aggsize=4m
BEGIN {began=timestamp;printf("READY host exact GL handoff target=%d\n",$target);}
pid$target::vmsvga_passthrough_opengl_wait_begin:d2,
pid$target::vmsvga_passthrough_opengl_exchange_unlocked:1dc,
pid$target::vmsvga_passthrough_opengl_scatter:48d
{self->native=timestamp;@begin=count();}
pid$target::vmsvga_passthrough_opengl_wait_end:entry
{
    @end=count();self->relock=timestamp;
    if(self->native){
        this->us=(timestamp-self->native)/1000;
        @native_count=count();@native_sum=sum(this->us);@native_max=max(this->us);@native_dist=quantize(this->us);
        self->native=0;
    }
}
pid$target::vmsvga_passthrough_opengl_wait_end:22
/self->relock/
{
    this->us=(timestamp-self->relock)/1000;
    @locked=count();@relock_sum=sum(this->us);@relock_max=max(this->us);@relock_dist=quantize(this->us);
    self->relock=0;
}
tick-1s /timestamp-began>=12000000000/ {exit(0);}
ERROR {printf("INVALID host GL handoff: probe error\n");exit(2);}
END {
    printf("BEGIN/END/RELOCK counts (require positive balanced actual coverage)\n");printa(@begin);printa(@end);printa(@locked);
    printf("UNLOCKED native+staging count/sum_us/max_us\n");printa(@native_count);printa(@native_sum);printa(@native_max);printa(@native_dist);
    printf("RELOCK from End entry to actual BQL acquired sum_us/max_us\n");printa(@relock_sum);printa(@relock_max);printa(@relock_dist);
    printf("END host exact GL handoff\n");
}
