/* Finite x64 owned-protocol survey. DGMessage is 16-byte header + args[16],
 * checked by passthrough-opengl-wire.h. VIMCGSDrawable's first three ints are
 * connection, window, surface; no Apple object layouts/pixels are decoded.
 * A repeated epoch/view is diagnostic evidence, NOT permission to skip an
 * import: private GPU allocation reuse and intervening writes need proof too.
 * Only compares consecutive imports in a protocol context; alternate cached
 * views are deliberately not counted as redundant.
 * Start real Finder motion only after READY. Instrumented times are not FPS.
 */
#pragma D option quiet
#pragma D option dynvarsize=16m
#pragma D option aggsize=4m
BEGIN {
    object[0]=0;incarnation[0]=0;epoch[0]=0;
    x[0]=0;y[0]=0;width[0]=0;height[0]=0;flip[0]=0;
    printf("READY Snow import epoch survey\n");
}
pid$target:GLEngine:VIMImportAcceleratorBacking:entry
{self->surface=*(uint32_t *)copyin(arg1+8,4);}
pid$target:GLEngine:VIMImportAcceleratorBacking:return {self->surface=0;}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:entry /arg2/
{
    this->header=(uint32_t *)copyin(arg1,16);
    self->import_at=0;
}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:entry
/arg2 && this->header[0]==0x10038/
{
    this->a=(uint64_t *)copyin(arg1+16,128);
    this->ctx=this->header[1];
    self->same=object[this->ctx]==this->a[10] &&
        incarnation[this->ctx]==this->a[11] && epoch[this->ctx]==this->a[12];
    self->same=self->same && x[this->ctx]==this->a[5] && y[this->ctx]==this->a[6];
    self->same=self->same && width[this->ctx]==this->a[7] &&
        height[this->ctx]==this->a[8] && flip[this->ctx]==this->a[9];
    object[this->ctx]=this->a[10];incarnation[this->ctx]=this->a[11];epoch[this->ctx]=this->a[12];
    x[this->ctx]=this->a[5];y[this->ctx]=this->a[6];
    width[this->ctx]=this->a[7];height[this->ctx]=this->a[8];flip[this->ctx]=this->a[9];
    self->width=this->a[7];self->height=this->a[8];
    self->import_surface=self->surface;self->import_at=timestamp;
}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:return /self->import_at/
{
    this->us=(timestamp-self->import_at)/1000;
    @calls[self->import_surface,self->width,self->height,self->same]=count();
    @wall[self->import_surface,self->width,self->height,self->same]=sum(this->us);
    @totals[self->import_surface,self->same]=count();
    self->import_at=0;
}
pid$target:libVIMPassthroughOpenGL.dylib:DGVRAMTransaction:return /self->surface/
{@outcomes[self->surface,(int32_t)arg1]=count();}
tick-12s {exit(0);}
ERROR {printf("INVALID: import survey error\n");exit(2);}
END {
    printa("surface=%x view=%ux%u same_epoch=%d calls=%@d wall_us=%@d\n",@calls,@wall);
    printa("TOTAL surface=%x same_epoch=%d calls=%@d\n",@totals);
    printa("RESULT surface=%x imported=%d calls=%@d\n",@outcomes);
}
