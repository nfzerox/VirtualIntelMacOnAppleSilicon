/* Bounded x86_64 client operation/caller survey. Packet header is our own
 * fixed-width ABI. No image contents or guessed Apple object offsets. */
#pragma D option quiet
#pragma D option dynvarsize=8m
BEGIN {began=timestamp;read_budget=32;printf("READY client traffic target=%d\n",$target);}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:entry
{
    this->h=(uint32_t *)copyin(arg1,16);this->op=this->h[0];
    this->context=this->h[1];this->bytes=this->h[2];
    @calls[this->op]=count();@bytes[this->op]=sum(this->bytes);
}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:entry
/this->op==0x10006 || this->op==307 || this->op==0x10044 || this->op==6 || this->op==4/
{@callers[this->op,ustack(12)]=count();}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:entry
/this->op==237 && read_budget>0/
{
    this->a=(uint64_t *)copyin(arg1+16,56);read_budget--;
    printf("READ context=%u xy=%d,%d size=%u,%u format=%x type=%x span=%u\n",
        this->context,this->a[0],this->a[1],this->a[2],this->a[3],this->a[4],this->a[5],this->bytes);
    @read_callers[ustack(14)]=count();
}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:entry
/this->op==301/
{
    this->a=(uint64_t *)copyin(arg1+16,64);
    @textures[this->a[2],this->a[3],this->a[4],this->a[6],this->a[7]]=count();
}
tick-1s /timestamp-began>=12000000000/ {exit(0);}
ERROR {printf("INVALID client traffic survey\n");exit(2);}
END {
    printf("opcode calls payload_bytes\n");printa("%x %@d %@d\n",@calls,@bytes);
    printa(@callers);printa(@read_callers);
    printf("texture internal width height format type calls\n");printa("%x %d %d %x %x %@d\n",@textures);
}
