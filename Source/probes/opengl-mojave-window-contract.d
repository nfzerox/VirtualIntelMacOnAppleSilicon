/* Finite, read-only app/native comparison. No pixels, pointer dereferences
 * beyond the first documented integer, or permanent instrumentation. */
#pragma D option quiet
#pragma D option bufsize=2m
BEGIN { budget=24;bounds_budget=24;printf("READY window contract pid=%u\n",$target); }
pid$target:GLEngine:gliSetInteger:entry
{
    self->setting=1;self->key=arg1;
    @sets[arg1]=count();
}
pid$target:GLEngine:gliSetInteger:return /self->setting/
{
    @set_results[self->key,(int)arg1]=count();
    self->setting=0;
}
pid$target:GLEngine:gliAttachDrawableWithOptions:entry /budget>0/
{
    budget--;self->attaching=1;
    printf("ATTACH context=%p type=%x screen=%u options=%x\n",arg0,arg1,arg3,arg4);
    ustack(6);
}
pid$target:GLEngine:gliAttachDrawableWithOptions:return /self->attaching/
{ printf("ATTACH RESULT %d\n",arg1);self->attaching=0; }
pid$target:OpenGL:CGLFlushDrawable:entry { @flushes=count(); }
/* This companion is for an i386 process: CGRect contains four floats. The
 * raw words avoid DTrace's lack of floating point and can be decoded offline. */
pid$target:SkyLight:SLSGetSurfaceBounds:entry /bounds_budget>0/
{ self->bounds_addr=arg3;self->bounds_surface=arg2; }
pid$target:SkyLight:SLSGetSurfaceBounds:return /self->bounds_addr/
{
    bounds_budget--;this->b=(uint32_t *)copyin(self->bounds_addr,16);
    printf("BOUNDS surface=%u result=%d bits=%x,%x,%x,%x\n",self->bounds_surface,arg1,this->b[0],this->b[1],this->b[2],this->b[3]);
    self->bounds_addr=0;
}
tick-20s { exit(0); }
END {
    printa("SET key=%x count=%@d\n",@sets);
    printa("SET RESULT key=%x result=%d count=%@d\n",@set_results);
    printa("FLUSH count=%@d\n",@flushes);
}
