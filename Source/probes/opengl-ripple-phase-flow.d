/* Finite read-only WindowServer ripple phase provenance.
 * Verify Mavericks CoreGraphics x64 ED5E3C37-7E2F-3885-8091-DFC9B44B38E8.
 * Matched disassembly of _CGXSetCIFilterValues at preferred 0x35d172
 * calls CFNumberGetValue(number, kCFNumberFloat32Type=5, record+0x30)
 * for inputPhase. Instruction +0xda is the verified next instruction after
 * that call: RBX still holds the type-2 filter record, ID at +0x14.
 * Read that explicit scalar field, never a private CF object or an XMM
 * return register. The CFNumber return probe was NOT observed even though
 * its entry fired in a fixed-phase control, so it is deliberately unused.
 * The owned DGPacket begins with DGMessage (wire v8): op/context at 0/4,
 * fixed uint64 arguments at 16. Generated op 602 is uniform1fARB with
 * location/value-bit arguments; 603 is uniform2fARB. Print bits for offline
 * float decoding. Empty filter observations do not prove absent phase updates;
 * require the activity counters and actual command rows before interpretation.
 * No pixel capture, writes, timing/FPS claim or production logging.
 * DTrace buffers from different CPUs must be sorted by timestamp offline.
 */
#pragma D option quiet
#pragma D option bufsize=4m
#pragma D option dynvarsize=4m
BEGIN {budget=4096;printf("READY ripple phase flow\n");}
pid$target:CoreGraphics:_CGXSetCIFilterValues:entry
{@activity[probefunc]=count();}
pid$target:CoreGraphics:_CGXSetCIFilterValues:da
/uregs[R_RBX] && budget>0/
{
    printf("%lu PHASE filter=%u bits=%x\n",(unsigned long)timestamp,
        *(uint32_t *)copyin(uregs[R_RBX]+0x14,4),
        *(uint32_t *)copyin(uregs[R_RBX]+0x30,4));budget--;
    @activity["phase-after-conversion"]=count();
}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:entry
/arg1 && budget>0 && *(uint32_t *)copyin(arg1,4)==602/
{
    this->header=(uint32_t *)copyin(arg1,8);
    this->args=(uint64_t *)copyin(arg1+16,16);
    printf("%lu UNIFORM context=%u location=%d bits=%x\n",(unsigned long)timestamp,
        this->header[1],(int32_t)this->args[0],(uint32_t)this->args[1]);budget--;
}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:entry
/arg1 && budget>0 && *(uint32_t *)copyin(arg1,4)==603/
{
    this->header=(uint32_t *)copyin(arg1,8);
    this->args=(uint64_t *)copyin(arg1+16,24);
    printf("%lu UNIFORM2 context=%u location=%d x=%x y=%x\n",(unsigned long)timestamp,
        this->header[1],(int32_t)this->args[0],(uint32_t)this->args[1],
        (uint32_t)this->args[2]);budget--;
}
pid$target:CoreGraphics:WSShaderProgramGetUniformLocation:entry
/arg2 && budget>0 && (copyinstr(arg2,64)=="inputPhase" ||
    copyinstr(arg2,64)=="mins" || copyinstr(arg2,64)=="maxs" ||
    copyinstr(arg2,64)=="screenSpaceOrigin")/
{
    printf("%lu NAME program=%p name=%s\n",(unsigned long)timestamp,
        (void *)arg1,copyinstr(arg2,64));budget--;
}
tick-15s {exit(0);}
END {printf("remaining record budget=%d\n",budget);printa(@activity);}
