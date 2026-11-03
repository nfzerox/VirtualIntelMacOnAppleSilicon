/* Finite command-order attribution, NOT clean pacing evidence. Inspect only
 * the fixed op/context prefix, never payloads/pixels/pointers. An observed
 * Begin is not a success ACK: qualify begins/rejections with the GLI counters.
 * Pass 1 for the pre-shared-draw materialization predicate, 0 for the corrected
 * predicate (array draws do not export private storage). The argument makes
 * baseline versus candidate attribution explicit; do not compare them as FPS. */
#pragma D option quiet
#pragma D option dynvarsize=4m
BEGIN {began=timestamp;printf("READY scoped first-private operations baseline=%d\n",$1);}
BEGIN /$1!=0 && $1!=1/ {exit(2);}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:entry
{
    this->m=(uint32_t *)copyin(arg1,16);this->op=this->m[0];this->context=this->m[1];this->bytes=this->m[2];
}
/* All target-process array packets, independently of scope eligibility. Only
 * fixed header/count metadata: determine whether oversized client-array
 * payloads warrant a separate investigation, never record vertex contents. */
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:entry /this->op==0x10006 || this->op==0x10020/
{
    this->vertices=*(uint64_t *)copyin(arg1+32,8);
    @draw_calls[this->op]=count();@draw_bytes[this->op]=sum(this->bytes);
    @draw_max[this->op]=max(this->bytes);@draw_vertices[this->op]=sum(this->vertices);
    @payload=quantize(this->bytes);
}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:entry /this->op==0x1004c/
{active[this->context]=1;seen[this->context]=0;}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:entry
/active[this->context] && !seen[this->context] && this->op>=0x10000 &&
 this->op!=0x1004c && this->op!=0x1004d && this->op!=0x1004e &&
 this->op!=0x10002 && this->op!=0x10007 && this->op!=0x10005 &&
 this->op!=0x10046 && this->op!=0x10004 && this->op!=0x10043 &&
 this->op!=0x1003e && this->op!=0x10047 && this->op!=0x1003a &&
 !(this->op>=0x1001a && this->op<=0x1001f) &&
 ($1 || (this->op!=0x10006 && this->op!=0x10020))/
{@first[this->op]=count();seen[this->context]=1;}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:entry /this->op==0x1004d/
{active[this->context]=0;}
tick-1s /timestamp-began>=12000000000/ {exit(0);}
END {
    printa("first_private_op=%x scopes=%@d\n",@first);
    printf("DRAW op calls payload_bytes max_payload submitted_vertices\n");
    printa("%x %@d %@d %@d %@d\n",@draw_calls,@draw_bytes,@draw_max,@draw_vertices);printa(@payload);
}
