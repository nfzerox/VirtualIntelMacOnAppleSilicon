/* Exact CoreGraphics x64 ED5E3C37-7E2F-3885-8091-DFC9B44B38E8.
 * Matched Ghidra/disassembly: begin_deferred_update_cycle arg2 is the update
 * state; +110/+118 are scheduled/next doubles and +120 the cycle flags.
 * MPGetNextWakeupTime follows device+158 -> description+278 -> public
 * StdFBShmem vblTime/vblDelta at +40/+48. No writes or guessed return XMMs.
 * Doubles are emitted as raw uint64 bits for OFFLINE decoding, not integer
 * approximations. This read-only, finite survey is not clean FPS evidence.
 */
#pragma D option quiet
#pragma D option dynvarsize=4m
#pragma D option aggsize=16m
BEGIN {began=timestamp;budget=8192;printf("READY compositor qualified deadlines target=%d\n",$target);}
pid$target:CoreGraphics:CGXUpdateDisplay:entry {self->depth++;}
pid$target:CoreGraphics:CGXUpdateDisplay:entry /self->depth==1/
{self->root=++roots;self->at=timestamp;self->finishes=0;self->state=(uint64_t)0;}
pid$target:CoreGraphics:CGXGLAccelFinish:entry /self->root/ {self->finishes++;}
pid$target:CoreGraphics:begin_deferred_update_cycle:entry /self->root && arg2 && budget>0/
{
    self->state=arg2;self->begin_depth++;
    this->state=(uint64_t *)copyin(arg2+0x110,16);
    @state[(timestamp-began)/1000,self->root,1,this->state[0],this->state[1],
        *(uint32_t *)copyin(arg2+0x120,4)]=count();budget--;
}
pid$target:CoreGraphics:begin_deferred_update_cycle:return /self->root && self->begin_depth && budget>0/
{
    this->state=(uint64_t *)copyin(self->state+0x110,16);
    @state[(timestamp-began)/1000,self->root,2,this->state[0],this->state[1],
        *(uint32_t *)copyin(self->state+0x120,4)]=count();budget--;
    @defer_results[arg1&255]=count();self->begin_depth--;
}
pid$target:CoreGraphics:MPGetNextWakeupTime:entry /self->root && arg0 && budget>0/
{
    this->description=*(uint64_t *)copyin(arg0+0x158,8);
    this->shared=*(uint64_t *)copyin(this->description+0x278,8);
    this->a=(uint64_t *)copyin(this->shared+0x40,16);
    this->b=(uint64_t *)copyin(this->shared+0x40,16);
    this->stable=this->a[0]==this->b[0] && this->a[1]==this->b[1];
    @clock_reads[this->stable]=count();
    @clocks[(timestamp-began)/1000,self->root,(int32_t)arg1,
        this->stable?this->a[0]:0,this->stable?this->a[1]:0,self->begin_depth!=0]=count();budget--;
}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->depth==1 && self->root && self->state && budget>0/
{
    this->state=(uint64_t *)copyin(self->state+0x110,16);
    @state[(timestamp-began)/1000,self->root,3,this->state[0],this->state[1],
        *(uint32_t *)copyin(self->state+0x120,4)]=count();budget--;
}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->depth==1 && self->root/
{
    @updates[(self->at-began)/1000,self->root,(timestamp-self->at)/1000,self->finishes]=count();
    self->root=0;self->state=0;self->begin_depth=0;
}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->depth/ {self->depth--;}
pid$target:AppleIntelHD5000GraphicsGLDriver*:glrFlushContextToken:entry {@intel=count();}
tick-1s /timestamp-began>=12000000000/ {exit(0);}
ERROR {printf("INVALID compositor deadline probe error\n");exit(2);}
END {
    printf("STATE at_us root phase(1=begin-entry,2=begin-return,3=update-return) scheduled_bits next_bits flags count\n");
    printa("%lu %u %u %lu %lu %u %@d\n",@state);
    printf("CLOCK at_us root selector vbl_ticks delta_ticks inside_begin count\n");
    printa("%lu %u %d %lu %lu %u %@d\n",@clocks);
    printf("UPDATES start_us root wall_us finishes count\n");printa("%lu %u %lu %u %@d\n",@updates);
    printf("Actual deferred bool results\n");printa(@defer_results);
    printf("Clock stability (0=reject,1=matched)\n");printa(@clock_reads);
    printf("Actual physical Intel submits (required for native oracle)\n");printa(@intel);
    printf("END compositor qualified deadlines budget=%d roots=%u began_ns=%lu\n",budget,roots,began);
}
