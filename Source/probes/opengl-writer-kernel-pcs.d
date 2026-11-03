/* Bounded sparse PC observation, NO pid-provider/FBT/per-call probes. This
 * exact-kernel clock read is qualified by UUID in the launch coordinator and
 * instruction bytes here. An unconditional target coverage count is mandatory:
 * absent selected kernel PCs alone never establish that a call was not on CPU.
 * No payload, pixel, key, inferior call, policy or kernel memory write. */
#pragma D option quiet
#pragma D option aggsize=16m
BEGIN {
    clock_function=(uintptr_t)&`dtrace_gethrtime;
    qualified=*(uint8_t*)(clock_function+0x25)==0x4c &&
        *(uint8_t*)(clock_function+0x26)==0x2b && *(uint8_t*)(clock_function+0x27)==0x35;
}
BEGIN /!qualified/ {printf("INVALID exact kernel clock layout\n");exit(2);}
BEGIN /qualified/ {
    epoch=*(uint64_t*)(clock_function+0x2c+*(int32_t*)(clock_function+0x28));
    began=timestamp;
    printf("READY sparse kernel PCs target=%d began_seconds=%u began_nanoseconds=%u epoch_seconds=%u epoch_nanoseconds=%u\n",
        $1,(uint32_t)(began/1000000000),(uint32_t)(began%1000000000),
        (uint32_t)(epoch/1000000000),(uint32_t)(epoch%1000000000));
}
profile-197 /pid==$1/ {@coverage[arg0!=0]=count();}
profile-197 /pid==$1 && arg0/ {@pcs[(timestamp-began)/1000,arg0,stack(18)]=count();}
tick-1s /timestamp-began>=24000000000/ {exit(0);}
ERROR {printf("INVALID sparse kernel PC capture\n");exit(2);}
END {
    printf("COVERAGE target user0 kernel1\n");printa(@coverage);
    printf("PCS relative_us actual_pc kernel_stack records (not durations)\n");printa(@pcs);
    printf("END sparse kernel PCs\n");
}
