/* Bounded metadata-only native9J3050 compositor contract. Use -Z -p the
 * actual WindowServer PID, then open/close a real menu. No pixel contents,
 * settings, persistent tracing or production renderer changes. The 48-byte
 * pbuffer record covers both measured period pointer widths. */
#pragma D option quiet
#pragma D option bufsize=2m
BEGIN { budget=128;printf("READY Leopard effect surfaces pid=%d\n",$target); }
pid$target:GLEngine:gliSetInteger:entry
/budget>0 && (arg1==0x3e5 || arg1==0x3e6 || arg1==0x3e7) && arg2/
{
    budget--;this->a=(uint32_t *)copyin(arg2,32);
    printf("IMPORT key=%x id=%x target=%x internal=%x w=%u h=%u tail=%x,%x,%x\n",
        arg1,this->a[0],this->a[1],this->a[2],this->a[3],this->a[4],
        this->a[5],this->a[6],this->a[7]);
}
pid$target:GLEngine:gliAttachDrawableWithOptions:entry
/budget>0 && arg2 && arg1==0x5a/
{
    budget--;printf("ATTACH type=%x screen=%x\n",arg1,arg3);
    tracemem(copyin(arg2,48),48);
}
pid$target:GLEngine:gliAttachDrawableWithOptions:entry
/budget>0 && arg2 && arg1!=0x5a/
{
    budget--;printf("ATTACH type=%x screen=%x\n",arg1,arg3);
    tracemem(copyin(arg2,12),12);
}
pid$target:GLEngine:VIMCreateAcceleratorDrawable:return
/budget>0/
{budget--;printf("ACCEL_RESULT=%x\n",(uint32_t)arg1);}
pid$target:GLEngine:VIMImportCGSTexture:return
/budget>0/
{budget--;printf("TEXTURE_RESULT=%x\n",(uint32_t)arg1);}
pid$target:CoreGraphics:cglsAttachPBuffer:entry
/budget>0 && arg1/
{
    budget--;self->pbuffer=1;printf("CGS_PBUFFER\n");tracemem(copyin(arg1,48),48);
}
pid$target:CoreGraphics:cglsAttachPBuffer:return
/self->pbuffer/
{printf("CGS_PBUFFER_RESULT=%x\n",(uint32_t)arg1);self->pbuffer=0;}
tick-12s {exit(0);}
