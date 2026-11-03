/* Generic packed-BGRA rectangle union. No CGS/WindowServer knowledge.
 * Plan once per vertical band, not a sort/clip scan for every pixel row.
 * Output is sorted disjoint stream spans, suitable for a masked receive. */
#ifndef DG_READ_MASK_H
#define DG_READ_MASK_H
#include <stdlib.h>
#include "passthrough-opengl-surfaces.h"
struct DGReadMaskRect { int x,y,width,height; };
struct DGReadMaskInterval { int left,right; };
static int DGCompareReadMaskIntervals(const void *a,const void *b)
{
    const struct DGReadMaskInterval *x=a,*y=b;
    return x->left<y->left?-1:x->left>y->left?1:0;
}
/* Caller supplies count intervals of scratch and capacity spans. Return -1
 * before submission if bounds/capacity do not fit, zero for an empty union. */
static int DGPlanReadMask(int width,int height,const struct DGReadMaskRect *rects,unsigned count,
    struct DGReadMaskInterval *intervals,struct DGReadSpan *spans,unsigned capacity)
{
    if(width<1 || height<1 || (uint64_t)width*height>UINT32_MAX/4 ||
       !rects || !intervals || !spans || !capacity)return -1;
    for(unsigned i=0;i<count;++i) {
        const struct DGReadMaskRect *r=&rects[i];
        if(r->x<0 || r->y<0 || r->width<1 || r->height<1 ||
           (int64_t)r->x+r->width>width || (int64_t)r->y+r->height>height)return -1;
    }
    unsigned used=0;
    for(int y=0;y<height;) {
        int next=height;unsigned active=0;
        for(unsigned i=0;i<count;++i) {
            const struct DGReadMaskRect *r=&rects[i];
            if(r->y>y) { if(r->y<next)next=r->y; }
            else if(r->y+r->height>y) {
                if(r->y+r->height<next)next=r->y+r->height;
                intervals[active++]=(struct DGReadMaskInterval){r->x,r->x+r->width};
            }
        }
        if(!active) { y=next;continue; }
        qsort(intervals,active,sizeof(*intervals),DGCompareReadMaskIntervals);
        unsigned merged=0;
        for(unsigned i=0;i<active;++i) {
            if(merged && intervals[i].left<=intervals[merged-1].right) {
                if(intervals[i].right>intervals[merged-1].right)intervals[merged-1].right=intervals[i].right;
            } else intervals[merged++]=intervals[i];
        }
        for(int row=y;row<next;++row)for(unsigned i=0;i<merged;++i) {
            uint32_t offset=((uint32_t)row*width+intervals[i].left)*4;
            uint32_t bytes=(uint32_t)(intervals[i].right-intervals[i].left)*4;
            if(used && spans[used-1].offset+spans[used-1].bytes==offset)spans[used-1].bytes+=bytes;
            else if(used<capacity)spans[used++]=(struct DGReadSpan){offset,bytes};
            else return -1;
        }
        y=next;
    }
    return (int)used;
}
#endif
