/* Owned x86_64 GLI only, MLGuestGLPair-Reattach build (SDK 10.9 CGL object,
 * 10.8 dispatch, backing-scope macro on, client profile off). Offsets come
 * from this exact source's clang record-layout output, NOT Apple internals.
 * Recompute VIMContext/VIMCGSDrawable offsets after any layout/build change. */
#pragma D option quiet
BEGIN { printf("READY owned import geometry\n"); }
pid$target:GLEngine:gliSetInteger:entry
/(arg1==0x3e5 || arg1==0x3e7) && arg2/
{
    this->d=*(uint64_t *)copyin(arg0+8216,8);
    this->a=(uint32_t *)copyin(arg2,20);
    self->d=this->d;self->op=arg1;
    self->sid=this->a[0];self->w=this->a[3];self->h=this->a[4];
}
pid$target:GLEngine:gliSetInteger:entry
/self->d && (arg1==0x3e5 || arg1==0x3e7)/
{
    this->v=(int32_t *)copyin(self->d,136);
    @views[self->op,self->sid,self->w,self->h,this->v[2],this->v[5],this->v[6],
        this->v[21],this->v[22],this->v[31],this->v[32],this->v[27]]=count();
    self->d=0;self->op=0;self->sid=0;self->w=0;self->h=0;
}
tick-12sec { exit(0); }
END { printf("op source requested_w/h current_id current_w/h backing_w/h origin_x/y unflipped calls\n");printa(@views); }
