/* Bounded effect correctness survey, identical on physical Intel and guest.
 * Observe the private GL surface-import contract, not source pixels. The
 * three/five/eight-word record sizes come from the matching native engine.
 * Run while entering/exiting Mission Control/Launchpad/Notification Center;
 * this deliberately instrumented run is not an FPS measurement.
 */
#pragma D option quiet
#pragma D option bufsize=4m
#pragma D option aggsize=8m
BEGIN { began=timestamp;printf("READY effect surface-import survey\n"); }
pid$target:CoreGraphics:*WSSetWindowTransform:entry,
pid$target:CoreGraphics:*WSSetWindowWarp:entry,
pid$target:CoreGraphics:*CGXSetWindowTransformList:entry
{
    /* Entry/cadence only: do not guess the private transform-record ABI. */
    @transforms[probefunc,(timestamp-began)/25000000]=count();
}
pid$target:GLEngine:*gliSetInteger:entry
{ @keys[arg1]=count(); }
pid$target:GLEngine:*gliSetInteger:entry
/(arg1==0x3e5 || arg1==0x3e7) && arg2/
{
    this->a=(uint32_t *)copyin(arg2,20);
    @imports[arg1,this->a[0],this->a[1],this->a[2],this->a[3],this->a[4],ustack(8)]=count();
}
pid$target:GLEngine:*gliSetInteger:entry
/arg1==0x3e5 && arg2/
{
    this->a=(uint32_t *)copyin(arg2,32);
    @flags[this->a[5],this->a[6],this->a[7]]=count();
}
pid$target:AppleIntelHD5000GraphicsGLDriver:*glrFlushContextToken*:entry
{ @intel=count(); }
tick-12sec { exit(0); }
END { printf("KEYS\n");printa(@keys);printf("IMPORTS\n");printa(@imports);
      printf("FLAGS\n");printa(@flags);printf("TRANSFORMS (25-ms buckets)\n");printa(@transforms);
      printf("INTEL\n");printa(@intel); }
