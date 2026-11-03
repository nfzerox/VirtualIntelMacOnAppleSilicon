/* OWNED research backends x64 UUID61033150-5A34-3E14-BB6C-0C0D04FD9F0E
 * and B5A0A56E-3D55-3DAC-9689-110FBE388C1F. BOTH actual standalone DWARFs
 * place DGConnection.batch at0xa198. Do NOT use
 * on another backend without requalifying that layout. Count real batch
 * writes inside synchronous submissions, under the connection lock. No
 * packet payloads, Apple object layouts or timing/FPS claims. A larger
 * combined exchange could avoid a crossing, NOT the native commands/fences.
 */
#pragma D option quiet
#pragma D option aggsize=4m
BEGIN {began=timestamp;self->sync=0;self->send=(uint64_t)0;printf("READY synchronous batch-drain shapes target=%d\n",$target);}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:entry
{
    self->sync=arg2;
}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:entry
/arg2/
{
    this->header=(uint32_t*)copyin(arg1,16);
    self->send=144+((this->header[3]&2)?0:this->header[2]);
    @queries=count();
}
pid$target:libVIMPassthroughOpenGL.dylib:DGConnectionTransfer:entry
/arg3/
{@all_writes=count();}
pid$target:libVIMPassthroughOpenGL.dylib:DGConnectionTransfer:entry
/self->sync && arg3 && arg1==arg0+0xa198/
{
    @drains=count();@bytes=sum(arg2);@size=quantize(arg2);
    @fits_64k[arg2+self->send+16<=65536]=count();
    @query_size=quantize(self->send);
}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:return {self->sync=0;}
tick-1s /timestamp-began>=12000000000/ {exit(0);}
ERROR {printf("INVALID batch-drain shapes: probe error\n");exit(2);}
END {
    printa("QUERIES %@d DRAINS %@d BYTES %@d\n",@queries,@drains,@bytes);
    printa("ALL method stream writes (probe coverage) %@d\n",@all_writes);
    printf("Would fit a64KiB combined request (false/true)\n");printa(@fits_64k);
    printf("Actual drained batch sizes\n");printa(@size);
    printf("Synchronous request sizes at those drains\n");printa(@query_size);
}
