/* Numeric region-contract survey, not an FPS test. The 12-byte
 * IOAccelDeviceRegion prefix is the actual 10.9 SDK uint32 count + four
 * int16 bounds. IOAccelSetSurfaceFramebufferShape's region is argument 1,
 * as verified in the matching IOKit/CoreGraphics call sites. */
#pragma D option quiet
#pragma D option aggsize=4m
BEGIN { printf("READY effect shape-region contract\n"); }
pid$target:IOKit:IOAccelSetSurfaceFramebufferShape:entry
/arg1/
{
    this->count=*(uint32_t *)copyin(arg1,4);
    this->bounds=(int16_t *)copyin(arg1+4,8);
    @shapes[arg0,arg2,arg3,this->count,this->bounds[0],this->bounds[1],this->bounds[2],this->bounds[3]]=count();
}
tick-12sec { exit(0); }
END { printf("connection options framebuffer rect-count x y width height calls\n");printa(@shapes); }
