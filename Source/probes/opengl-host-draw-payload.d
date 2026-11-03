/* Bounded metadata survey of OUR fixed-width wire, not Apple object layouts.
 * DGMessage is asserted 144 bytes in passthrough-opengl-wire.h. Attach only after
 * checking the actual loaded executor UUID. No pixels or Apple objects are
 * read. This per-draw instrumentation is NOT a presentation benchmark.
 * If stock-host policy denies copyin, do not change host security: -D
 * VIM_GUEST_DRAW=1 selects the actual guest client DGSubmit entry instead.
 * Its packet starts with the identical DGMessage; require x64 for this probe.
 * Nonzero first establishes possible prefix overcapture, not its cost or a
 * license to rebase gl_VertexID. The optional guest survey below inspects
 * bounded index/array metadata. -D VIM_DRAW_METADATA_ONLY=1 omits those probes;
 * its indexed minimum remains UNKNOWN.
 */
#pragma D option quiet
#pragma D option dynvarsize=8m
#pragma D option aggsize=8m
struct VIMDrawMessage {
    uint32_t op, context, bytes, flags;
    uint64_t args[16];
};
/* Explicitly bounded x64 guest packet metadata only; 10.9 DTrace has no loop.
 * DGPacket.data is immediately after its 144-byte message. Inspect at most
 * 128 ushort indices, never infer the minimum from a partial index list. */
#define MININDEX(I) this->lo=(this->n>(I) && this->index[I]<this->lo)?this->index[I]:this->lo;
#define MIN8(I) MININDEX(I) MININDEX((I)+1) MININDEX((I)+2) MININDEX((I)+3) MININDEX((I)+4) MININDEX((I)+5) MININDEX((I)+6) MININDEX((I)+7)
#define MIN32(I) MIN8(I) MIN8((I)+8) MIN8((I)+16) MIN8((I)+24)
BEGIN {began=timestamp;printf("READY draw payload target=%d header=%d\n",$target,sizeof(struct VIMDrawMessage));}
#ifdef VIM_GUEST_DRAW
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:entry
{this->op=*(uint32_t*)copyin(arg1,4);}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:entry
/this->op==0x10006 || this->op==0x10020/
#else
pid$target:libVIMPassthroughOpenGLHost.dylib:DGHostDraw:entry
#endif
{
    this->m=(uint64_t*)copyin(arg1,sizeof(struct VIMDrawMessage));
    this->type=(unsigned long long)this->m[5];this->first=this->m[3];
    this->ebo=this->m[6]!=0;
    /* Scalar copies survive later clauses; copyin scratch pointers do not. */
    this->n=this->m[4];this->payload=(uint32_t)this->m[1];this->index_offset=this->m[7];
    @draws[this->type,this->ebo,this->first!=0]=count();
    @payload[this->type,this->ebo,this->first!=0]=sum((uint32_t)this->m[1]);
    @vertices[this->type,this->ebo,this->first!=0]=sum(this->m[4]);
    @first[this->type,this->ebo]=quantize(this->first);
    @count[this->type,this->ebo]=quantize(this->m[4]);
    @bytes[this->type,this->ebo]=quantize((uint32_t)this->m[1]);
    @descriptors[this->type,this->ebo]=quantize(this->m[10]);
}
#if defined(VIM_GUEST_DRAW) && !defined(VIM_DRAW_METADATA_ONLY)
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:entry
/this->op==0x10006 && this->type==5123 && !this->ebo && this->n>0 && this->n<=128 &&
 this->index_offset>0 && this->index_offset-1<=this->payload &&
 this->n*2<=this->payload-(this->index_offset-1)/
{
    this->data=*(uint64_t*)copyin(arg1+144,8);
    this->lo=65535;
    this->index=(uint16_t*)copyin(this->data+this->index_offset-1,this->n*2);
    MIN32(0) MIN32(32) MIN32(64) MIN32(96)
    @indexed_min=quantize(this->lo);
    @indexed_min_calls[this->lo!=0]=count();
    @indexed_min_payload[this->lo!=0]=sum(this->payload);
}
pid$target:libVIMPassthroughOpenGL.dylib:DGDrawInstances:entry
{self->draw_depth++;self->first_base=0;self->first_end=0;}
pid$target:libVIMPassthroughOpenGL.dylib:DGPacketPointer:entry
/self->draw_depth && arg1==9 && arg2 && arg3 && arg2+arg3>arg2/
{
    this->end=arg2+arg3;
    this->overlap_begin=arg2>self->first_base?arg2:self->first_base;
    this->overlap_end=this->end<self->first_end?this->end:self->first_end;
    this->overlap=self->first_end && this->overlap_end>this->overlap_begin?
        this->overlap_end-this->overlap_begin:0;
    @array_copies=count();@array_copy_bytes=sum(arg3);
    @first_overlap_bytes=sum(this->overlap);@array_overlap=quantize(this->overlap);
}
pid$target:libVIMPassthroughOpenGL.dylib:DGPacketPointer:entry
/self->draw_depth && arg1==9 && arg2 && arg3 && arg2+arg3>arg2 && !self->first_end/
{self->first_base=arg2;self->first_end=arg2+arg3;}
pid$target:libVIMPassthroughOpenGL.dylib:DGDrawInstances:return /self->draw_depth/
{self->draw_depth--;self->first_base=0;self->first_end=0;}
#endif
tick-1s /timestamp-began>=18000000000/ {exit(0);}
ERROR {printf("INVALID draw survey: probe error\n");exit(2);}
END {
    printf("DRAW type ebo nonzero_first calls payload_bytes vertices\n");
    printa("%llu %d %d %@d %@d %@d\n",@draws,@payload,@vertices);
    printf("FIRST (indexed minimum UNKNOWN)\n");printa(@first);
    printf("COUNT\n");printa(@count);
    printf("BYTES\n");printa(@bytes);
    printf("DESCRIPTORS\n");printa(@descriptors);
#if defined(VIM_GUEST_DRAW) && !defined(VIM_DRAW_METADATA_ONLY)
    printf("EXACT MINIMUM: non-instanced ushort client indices, complete count<=128 ONLY\n");
    printa(@indexed_min);
    printa("nonzero_minimum=%d calls=%@d payload=%@d\n",@indexed_min_calls,@indexed_min_payload);
    printf("CPU array copies, total bytes, definite duplicate bytes overlapping FIRST span\n");
    printa("%@d %@d %@d\n",@array_copies,@array_copy_bytes,@first_overlap_bytes);printa(@array_overlap);
#endif
}
