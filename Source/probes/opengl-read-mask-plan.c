/* Owned arrays only: compare the optimized union against a pixel oracle. */
#include <stdio.h>
#include <string.h>
#include "../PassthroughOpenGL/passthrough-opengl-read-mask.h"
static unsigned seed=0x619372;
static unsigned number(unsigned limit) { seed=seed*1664525+1013904223;return seed%limit; }
int main(void)
{
    enum { W=97,H=53,N=24,C=W*H };
    struct DGReadMaskRect rects[N];struct DGReadMaskInterval scratch[N];
    struct DGReadSpan spans[C];unsigned char expected[W*H],actual[W*H];
    for(unsigned test=0;test<1000;++test) {
        unsigned count=number(N+1);memset(expected,0,sizeof(expected));memset(actual,0,sizeof(actual));
        for(unsigned i=0;i<count;++i) {
            int x=number(W),y=number(H),w=1+number(W-x),h=1+number(H-y);
            rects[i]=(struct DGReadMaskRect){x,y,w,h};
            for(int row=y;row<y+h;++row)memset(expected+row*W+x,1,w);
        }
        int n=DGPlanReadMask(W,H,rects,count,scratch,spans,C);
        if(n<0)return 1;
        uint32_t end=0;
        for(int i=0;i<n;++i) {
            if(!spans[i].bytes || (spans[i].offset&3) || (spans[i].bytes&3) ||
               spans[i].offset<end || spans[i].offset+spans[i].bytes>W*H*4)return 2;
            memset(actual+spans[i].offset/4,1,spans[i].bytes/4);end=spans[i].offset+spans[i].bytes;
        }
        if(memcmp(actual,expected,sizeof(actual)))return 3;
    }
    rects[0]=(struct DGReadMaskRect){0,0,W,H};
    if(DGPlanReadMask(W,H,rects,1,scratch,spans,1)!=1 || spans[0].offset || spans[0].bytes!=W*H*4)return 4;
    rects[0]=(struct DGReadMaskRect){1,0,W-2,H};
    if(DGPlanReadMask(W,H,rects,1,scratch,spans,H-1)!=-1)return 5;
    rects[0].x=-1;if(DGPlanReadMask(W,H,rects,1,scratch,spans,C)!=-1)return 6;
    rects[0]=(struct DGReadMaskRect){W-1,H-1,2,1};
    if(DGPlanReadMask(W,H,rects,1,scratch,spans,C)!=-1)return 7;
    puts("PASS 1000 rectangle unions, overlap/adjacency, empty/full mask, bounded capacity and invalid edges");return 0;
}
