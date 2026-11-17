/* OWNED GLEngine only: V30-RectangleImport-Payloads/Builds/lion, x86_64.
 * DWARF SBType verifies VIMContext.drawable=8944 and VIMCGSDrawable offsets
 * below. Compare the loaded library with that artifact before using this.
 * Never apply these offsets to Apple binaries or another guest/build.
 * Finite metadata, not source pixels or timing evidence. */
#pragma D option quiet
#pragma D option bufsize=4m
BEGIN { printf("READY owned Lion backdrop geometry\n"); }
pid$target:GLEngine:gliSetInteger:entry
/(arg1==0x3e5 || arg1==0x3e6 || arg1==0x3e7) && arg2 && events++<256/
{
    self->d=*(uint64_t *)copyin(arg0+8944,8);
}
pid$target:GLEngine:gliSetInteger:entry
/self->d/
{
    this->a=(uint32_t *)copyin(arg2,20);
    this->v=(int32_t *)copyin(self->d,136);
    printf("key=%x import=%u extent=%ux%u current=%u drawable=%dx%d backing=%dx%d unflipped=%d region=%d origin=%d,%d family=%u\n",
        arg1,this->a[0],this->a[3],this->a[4],this->v[2],this->v[5],this->v[6],
        this->v[21],this->v[22],this->v[27],this->v[30],this->v[31],this->v[32],this->v[33]);
    self->d=0;
}
tick-15s { exit(0); }
