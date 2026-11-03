/* Attribute pauses BEFORE the first Finish/Mirror as well as inside them.
 * A sparse sample belongs to one actual CGXUpdateDisplay invocation; it is
 * not a GPU timer, clean FPS, or proof that vtimestamp is host CPU work.
 * No private Apple layouts, pixels, guest pointers or all-mutex tracing.
 * Bounded, buffered, read-only; use the READY-coordinated real-input harness.
 */
#pragma D option quiet
#pragma D option dynvarsize=16m
#pragma D option aggsize=32m
BEGIN {began=timestamp;budget=1024;printf("READY compositor whole-root hotspots target=%d\n",$target);}
pid$target:CoreGraphics:CGXUpdateDisplay:entry {self->depth++;}
pid$target:CoreGraphics:CGXUpdateDisplay:entry /self->depth==1/
{
    self->root=++roots;self->at=timestamp;self->finishes=0;self->mirror=0;
    self->io_depth=0;self->io_us=(uint64_t)0;
    root_by_tid[tid]=self->root;finish_by_tid[tid]=0;
    mirror_by_tid[tid]=0;io_by_tid[tid]=0;
}
pid$target:CoreGraphics:CGXGLAccelFinish:entry /self->root/
{self->finishes++;finish_by_tid[tid]=self->finishes;}
pid$target:CoreGraphics:CGXUpdateMagicMirror:entry /self->root/
{self->mirror++;mirror_by_tid[tid]=self->mirror;}
pid$target:CoreGraphics:CGXUpdateMagicMirror:return /self->root && self->mirror/
{self->mirror--;mirror_by_tid[tid]=self->mirror;}
pid$target:IOKit:IOConnectCallMethod:entry,
pid$target:IOKit:IOConnectCallStructMethod:entry,
pid$target:IOKit:IOConnectCallScalarMethod:entry,
pid$target:IOKit:IOConnectTrap4:entry,
pid$target:IOKit:IOConnectTrap6:entry
/self->root && !self->io_depth/
{
    self->io_at=timestamp;self->io_kind=probefunc;self->io_selector=(uint32_t)arg1;
    @io_entered[probefunc]=count();
}
pid$target:IOKit:IOConnectCallMethod:entry,
pid$target:IOKit:IOConnectCallStructMethod:entry,
pid$target:IOKit:IOConnectCallScalarMethod:entry,
pid$target:IOKit:IOConnectTrap4:entry,
pid$target:IOKit:IOConnectTrap6:entry
/self->root/ {self->io_depth++;io_by_tid[tid]=self->io_depth;}
pid$target:IOKit:IOConnectCallMethod:return,
pid$target:IOKit:IOConnectCallStructMethod:return,
pid$target:IOKit:IOConnectCallScalarMethod:return,
pid$target:IOKit:IOConnectTrap4:return,
pid$target:IOKit:IOConnectTrap6:return
/self->root && self->io_depth==1/
{
    this->us=(timestamp-self->io_at)/1000;self->io_us+=this->us;
    @io_returned[self->io_kind]=count();
    @io_max[self->io_kind]=max(this->us);
}
pid$target:IOKit:IOConnectCallMethod:return,
pid$target:IOKit:IOConnectCallStructMethod:return,
pid$target:IOKit:IOConnectCallScalarMethod:return,
pid$target:IOKit:IOConnectTrap4:return,
pid$target:IOKit:IOConnectTrap6:return
/self->root && self->io_depth==1 && timestamp-self->io_at>=1000000 && budget>0/
{
    @slow[self->root,self->io_kind,self->io_selector,
        self->finishes!=0,self->mirror!=0,(timestamp-self->io_at)/1000]=count();
    @callers[self->root,self->io_kind,ustack(20)]=count();budget--;
}
pid$target:IOKit:IOConnectCallMethod:return,
pid$target:IOKit:IOConnectCallStructMethod:return,
pid$target:IOKit:IOConnectCallScalarMethod:return,
pid$target:IOKit:IOConnectTrap4:return,
pid$target:IOKit:IOConnectTrap6:return
/self->root && self->io_depth/ {self->io_depth--;io_by_tid[tid]=self->io_depth;}
profile-97 /pid==$target/ {@all_samples=count();}
profile-97 /pid==$target && root_by_tid[tid]/
{
    @samples[root_by_tid[tid],finish_by_tid[tid]!=0,mirror_by_tid[tid]!=0,
        io_by_tid[tid]!=0,arg0?"kernel":"user"]=count();
    @users[root_by_tid[tid],finish_by_tid[tid]!=0,ustack(24)]=count();
}
profile-97 /pid==$target && root_by_tid[tid] && arg0/
{@kernels[root_by_tid[tid],finish_by_tid[tid]!=0,stack(24)]=count();}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->depth==1 && self->root && budget>0/
{
    @updates[(self->at-began)/1000,self->root,(timestamp-self->at)/1000,
        self->io_us,self->finishes]=count();budget--;
}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->depth==1/
{self->root=0;root_by_tid[tid]=0;}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->depth/ {self->depth--;}
tick-1s /timestamp-began>=12000000000/ {exit(0);}
ERROR {printf("INVALID compositor whole-root hotspot probe error\n");exit(2);}
END {
    printf("UPDATES start_us root wall_us outer_io_union_us finishes count\n");
    printa("%lu %u %lu %lu %u %@d\n",@updates);
    printf("SLOW_IO root kind selector after_first_finish inside_mirror wall_us count\n");
    printa("%u %s %u %u %u %lu %@d\n",@slow);
    printf("IO_BALANCE kind entered returned maximum_us\n");
    printa("%s %@d %@d %@d\n",@io_entered,@io_returned,@io_max);
    printf("ROOT_SAMPLES root after_first_finish inside_mirror inside_io execution count\n");
    printa("%u %u %u %u %s %@d\n",@samples);
    printf("Slow I/O callers by actual root\n");printa(@callers);
    printf("Actual root user stacks (before/after first Finish)\n");printa(@users);
    printf("Actual root kernel stacks (before/after first Finish)\n");printa(@kernels);
    printf("ALL target samples (positive coverage, not duration)\n");printa(@all_samples);
    printf("END compositor whole-root hotspots budget=%d roots=%u\n",budget,roots);
}
