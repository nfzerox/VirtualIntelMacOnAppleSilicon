/* Statistical RUNNING-CPU attribution, separate from wall/ownership waits.
 * No GL/IOKit entry traps, all-lock hooks, task suspension, private fields,
 * pixel reads or writes. Sleeping CPU/GPU waits are deliberately NOT measured.
 * Physical reference requires positive Intel-driver submits and gpuswitch0.
 * Treat counts as sampled classes, never clean frame rates or a speed ratio.
 */
#pragma D option quiet
#pragma D option aggsize=16m
BEGIN {began=timestamp;printf("READY compositor CPU-only target=%d\n",$target);}
profile-499 /pid==$target/
{
    @execution[arg0?"kernel":"user"]=count();
    @users[tid,arg0?"kernel":"user",ustack(24)]=count();
}
profile-499 /pid==$target && arg0/
{
    /* A frameless callee can disappear from stack(); retain the actual
     * interrupted PC too. In a VM, port-exit time/resumption biases these
     * samples: they are NOT proof that all charged time executed guest code. */
    @sites[tid,sym(arg0)]=count();@kernels[tid,stack(24)]=count();
}
pid$target:AppleIntelHD5000GraphicsGLDriver*:glrFlushContextToken:entry {@intel=count();}
tick-1s /timestamp-began>=12000000000/ {exit(0);}
ERROR {printf("INVALID compositor CPU-only probe error\n");exit(2);}
END {
    printf("RUNNING_EXECUTION class samples\n");printa(@execution);
    printf("RUNNING_USER_STACKS tid execution user_stack samples\n");printa(@users);
    printf("RUNNING_KERNEL_STACKS tid kernel_stack samples\n");printa(@kernels);
    printf("INTERRUPTED_KERNEL_PC tid actual_pc samples\n");printa(@sites);
    printf("Actual physical Intel submits (required for reference)\n");printa(@intel);
    printf("END compositor CPU-only\n");
}
