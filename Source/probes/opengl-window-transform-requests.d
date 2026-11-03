/* Matching 13F1911 x86_64 CoreGraphics only, UUID
 * ED5E3C37-7E2F-3885-8091-DFC9B44B38E8. Read-only, bounded metadata.
 * Disassembled CGXSetWindowTransformList entry:
 * RSI=uint32 window IDs, EDX=count; seventh argument [RSP+8] points to
 * 48-byte six-double affine transforms, eighth [RSP+16] is transform count.
 * Transform i saturates at transform-count-1. Print at most the first two
 * windows per request, as raw double BITS for offline decoding. This is not
 * all-window coverage, pixel validation, or an instrumented FPS assertion.
 */
#pragma D option quiet
#pragma D option bufsize=4m
BEGIN {began=timestamp;budget=512;printf("READY matched affine transform requests\n");}
pid$target:CoreGraphics:CGXSetWindowTransformList:entry
{@requests[arg2]=count();}
pid$target:CoreGraphics:CGXSetWindowTransformList:entry
/arg1 && arg2>0 && arg2<=256 && budget>0/
{
    this->n=*(uint32_t *)copyin(uregs[R_RSP]+16,4);
    this->base=*(uint64_t *)copyin(uregs[R_RSP]+8,8);
    this->ids=arg1;this->windows=arg2;
}
pid$target:CoreGraphics:CGXSetWindowTransformList:entry
/arg1 && arg2>0 && arg2<=256 && budget>0 && this->n>0 && this->n<=256 && this->base/
{
    this->m=(uint64_t *)copyin(this->base,48);
    printf("%lu AFFINE tid=%d index=0 id=%u windows=%u transforms=%u bits=%llx,%llx,%llx,%llx,%llx,%llx\n",
        (unsigned long)timestamp,(int)tid,*(uint32_t *)copyin(this->ids,4),
        (uint32_t)this->windows,this->n,this->m[0],this->m[1],this->m[2],this->m[3],this->m[4],this->m[5]);budget--;
}
pid$target:CoreGraphics:CGXSetWindowTransformList:entry
/arg1 && arg2>1 && arg2<=256 && budget>0 && this->n>0 && this->n<=256 && this->base/
{
    this->m=(uint64_t *)copyin(this->base+(this->n>1?48:0),48);
    printf("%lu AFFINE tid=%d index=1 id=%u windows=%u transforms=%u bits=%llx,%llx,%llx,%llx,%llx,%llx\n",
        (unsigned long)timestamp,(int)tid,*(uint32_t *)copyin(this->ids+4,4),
        (uint32_t)this->windows,this->n,this->m[0],this->m[1],this->m[2],this->m[3],this->m[4],this->m[5]);budget--;
}
tick-1s /timestamp-began>=12000000000/ {exit(0);}
END {printf("remaining metadata budget=%d; requests by window count\n",budget);printa(@requests);}
