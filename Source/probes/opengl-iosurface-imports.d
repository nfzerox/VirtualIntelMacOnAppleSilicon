/* Finite GLI surface-import survey. The eight words are the existing
 * gliSetInteger(0x38e) decoder's record, not native private object offsets.
 * Reports formats, dimensions and real return errors; never surface pixels.
 * Use -Z against WindowServer or a WebKit process, wait for READY, reload
 * the local compositing reference, and retain the actual Intel renderer check
 * when using this on the physical reference Mac. */
#pragma D option quiet
#pragma D option bufsize=2m
BEGIN { printf("READY IOSurface import survey\n"); }
pid$target:GLEngine:gliSetInteger:entry
{
    self->key=arg1;
    @keys[arg1]=count();
}
pid$target:GLEngine:gliSetInteger:entry
/arg1==0x38e && arg2/
{
    this->a=(uint32_t *)copyin(arg2,32);
    @imports[this->a[1],this->a[2],this->a[3],this->a[4],this->a[5],this->a[6],this->a[7]]=count();
}
pid$target:GLEngine:gliSetInteger:return
{ @returns[self->key,(uint32_t)arg1]=count();self->key=0; }
tick-10sec { exit(0); }
END {
    printf("KEYS\n");printa(@keys);
    printf("IMPORTS target internal width height format type plane count\n");printa(@imports);
    printf("RETURNS key error count\n");printa(@returns);
}
