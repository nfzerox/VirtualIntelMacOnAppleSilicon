/* Finite metadata-only trace for Aperture's loupe/activation repro. Run on
 * native Intel and the guest; no app patches, pixels or private driver offsets.
 * CGRect is four doubles in these x86_64 processes, printed as raw bits since
 * DTrace does not support floating point. Decode offline, not in production.
 */
#pragma D option quiet
#pragma D option bufsize=4m
BEGIN { budget=1600;printf("READY loupe pid=%u\n",$target); }
pid$target::CGLSetSurface:entry /budget>0/
{ budget--;printf("%llu SET ctx=%p cid=%x wid=%u sid=%u\n",timestamp,arg0,arg1,arg2,arg3); }
pid$target::SLSBindSurface:entry /budget>0/
{ budget--;printf("%llu BIND cid=%x wid=%u sid=%u mode=%u first=%x second=%x\n",timestamp,arg0,arg1,arg2,arg3,arg4,arg5); }
pid$target::SLSReleaseWindow:entry /budget>0/
{ budget--;printf("%llu RELEASE cid=%x wid=%u\n",timestamp,arg0,arg1);ustack(5); }
pid$target::SLSSetWindowOpacity:entry /budget>0/
{ budget--;printf("%llu OPACITY cid=%x wid=%u opaque=%u\n",timestamp,arg0,arg1,arg2); }
pid$target::SLSGetSurfaceBounds:entry /budget>0/
{ self->bounds=arg3;self->wid=arg1;self->sid=arg2; }
pid$target::SLSGetSurfaceBounds:return /self->bounds/
{
    budget--;this->b=(uint64_t *)copyin(self->bounds,32);
    printf("%llu BOUNDS wid=%u sid=%u err=%d bits=%llx,%llx,%llx,%llx\n",timestamp,self->wid,self->sid,arg1,this->b[0],this->b[1],this->b[2],this->b[3]);self->bounds=0;
}
pid$target::SLSSetSurfaceBounds:entry /budget>0/
{
    budget--;printf("%llu SETBOUNDS cid=%x wid=%u sid=%u rect=%p\n",timestamp,arg0,arg1,arg2,arg3);
}
pid$target::SLSSetSurfaceOpacity:entry /budget>0/
{ budget--;printf("%llu SURFACEOPACITY cid=%x wid=%u sid=%u opaque=%u\n",timestamp,arg0,arg1,arg2,arg3); }
pid$target::VIMCreateDrawable:entry /budget>0/
{ budget--;printf("%llu BITMAP ctx=%p\n",timestamp,arg0); }
pid$target::VIMPreserveBitmapBacking:entry /budget>0/
{ budget--;printf("%llu PRESERVE drawable=%p\n",timestamp,arg0); }
pid$target::VIMPresentBitmapRAM:entry { @present=count(); }
pid$target::CGLFlushDrawable:entry { @flush[arg0]=count(); }
pid$target::CGLUpdateContext:entry /budget>0/
{ budget--;printf("%llu UPDATE ctx=%p\n",timestamp,arg0); }
tick-30s { exit(0); }
END { printa("PRESENT %@u\n",@present);printa("FLUSH ctx=%p count=%@u\n",@flush);printf("BUDGET %d\n",budget); }
