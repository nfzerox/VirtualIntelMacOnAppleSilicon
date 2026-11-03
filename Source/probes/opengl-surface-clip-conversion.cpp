/* Exact kernel conversion, no mock implementation or kernel-policy change. */
#include <IOKit/graphics/IOAccelSurfaceConnect.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../vendor/source/VMsvga2/vminclude/vim_framebuffer.h"
#include "../vendor/source/VMsvga2/vminclude/vim_surface_clip.h"
static int failures;
static void check(const char *label,bool good)
{ printf("%s %s\n",good?"PASS":"FAIL",label);failures+=!good; }
int main()
{
    struct { uint32_t count;IOAccelBounds bounds,rects[VIM_SURFACE_CLIP_RECTS+1]; } input;
    memset(&input,0,sizeof(input));input.bounds=(IOAccelBounds){31,47,64,48};
    const IOAccelDeviceRegion *region=reinterpret_cast<const IOAccelDeviceRegion *>(&input);
    VIMSurfaceClip output;memset(&output,0,sizeof(output));
    check("zero count becomes exactly one bounds rectangle",
        VIMSurfaceClipFromRegion(region,sizeof(IOAccelDeviceRegion),0,0,&output) &&
        output.version==VIM_SURFACE_CLIP_VERSION && output.count==1 &&
        !memcmp(&output.rects[0],&input.bounds,sizeof(input.bounds)) &&
        output.origin_x==31 && output.origin_y==47);
    check("primary view origin is independent of bounds coordinates",
        VIMSurfaceClipFromRegion(region,sizeof(IOAccelDeviceRegion),31,47,&output) &&
        !output.origin_x && !output.origin_y && output.rects[0].x==31 && output.rects[0].y==47);
    input.count=2;input.rects[0]=(IOAccelBounds){33,51,20,30};input.rects[1]=(IOAccelBounds){61,51,12,30};
    check("explicit disjoint rectangles and signed origin retained",
        VIMSurfaceClipFromRegion(region,sizeof(input),40,60,&output) && output.count==2 &&
        output.origin_x==-9 && output.origin_y==-13 &&
        !memcmp(output.rects,input.rects,2*sizeof(IOAccelBounds)));
    input.count=1;input.rects[0]=(IOAccelBounds){31,47,0,0};
    check("explicit zero extent is not expanded to bounds",
        VIMSurfaceClipFromRegion(region,sizeof(input),0,0,&output) && output.count==1 &&
        !output.rects[0].w && !output.rects[0].h);
    input.count=VIM_SURFACE_CLIP_RECTS;
    for(unsigned i=0;i<input.count;++i)input.rects[i]=(IOAccelBounds){(int16_t)i,1,1,1};
    check("maximum inline region preserves final rectangle",
        VIMSurfaceClipFromRegion(region,sizeof(input)-sizeof(IOAccelBounds),0,0,&output) &&
        output.count==VIM_SURFACE_CLIP_RECTS && output.rects[VIM_SURFACE_CLIP_RECTS-1].x==VIM_SURFACE_CLIP_RECTS-1);
    check("truncated rectangle array rejected",!VIMSurfaceClipFromRegion(region,
        sizeof(input)-sizeof(IOAccelBounds)-1,0,0,&output));
    ++input.count;
    check("too many rectangles rejected",!VIMSurfaceClipFromRegion(region,sizeof(input),0,0,&output));
    check("truncated header rejected",!VIMSurfaceClipFromRegion(region,sizeof(IOAccelDeviceRegion)-1,0,0,&output));
    check("null arguments rejected",!VIMSurfaceClipFromRegion(NULL,0,0,0,&output) &&
        !VIMSurfaceClipFromRegion(region,sizeof(input),0,0,NULL));
    return failures?1:0;
}
