/* Bounded guest diagnostic: one read-only snapshot of renderer-global VRAM
 * transfer counters. Does not attach WindowServer or touch its surfaces. */
#include <OpenGL/gl.h>
#include <stdio.h>
#include "../PassthroughOpenGL/passthrough-opengl-surfaces.h"
extern void *DGCreateContext(const int *,void *);
extern GLboolean DGMakeCurrent(void *,void *,GLenum,GLsizei,GLsizei);
extern void DGDestroyContext(void *);
int main(void)
{
    unsigned char pixels[64]={0};void *c=DGCreateContext(NULL,NULL);uint64_t counts[4]={0};
    int good=c && DGMakeCurrent(c,pixels,GL_UNSIGNED_BYTE,4,4) && DGVRAMStats(counts);
    if(good)printf("{\"imports\":%llu,\"publishes\":%llu,\"import_bytes\":%llu,\"publish_bytes\":%llu}\n",
        (unsigned long long)counts[0],(unsigned long long)counts[1],(unsigned long long)counts[2],(unsigned long long)counts[3]);
    else fputs("Shared VRAM capability/statistics unavailable\n",stderr);
    if(c)DGDestroyContext(c);return good?0:1;
}
