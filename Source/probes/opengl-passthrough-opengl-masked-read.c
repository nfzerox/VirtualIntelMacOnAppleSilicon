/* Own offscreen GPU pixels and heap allocations only. No screen capture. */
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <IOKit/IOKitLib.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <string.h>
#include "../PassthroughOpenGL/passthrough-opengl-read-mask.h"
#include "../vendor/source/VMsvga2/vminclude/vim_passthrough_opengl.h"
#include "../vendor/source/VMsvga2/AC/UCMethods.h"
extern void *DGCreateContext(const int *,void *);
extern GLboolean DGMakeCurrent(void *,void *,GLenum,GLsizei,GLsizei);
extern void DGDestroyContext(void *);
static double seconds(void)
{ mach_timebase_info_data_t t;mach_timebase_info(&t);return mach_absolute_time()*(double)t.numer/t.denom/1e9; }
static void copy_rows(unsigned char *destination,size_t stride,const unsigned char *reference,
    const struct DGReadSpan *spans,int count,int width,int height,int reverse)
{
    for(int i=0;i<count;++i) {
        uint32_t offset=spans[i].offset,remaining=spans[i].bytes,row_bytes=(uint32_t)width*4;
        while(remaining) {
            uint32_t row=offset/row_bytes,column=offset%row_bytes,bytes=remaining<row_bytes-column?remaining:row_bytes-column;
            memcpy(destination+(reverse?height-1-row:row)*stride+column,reference+offset,bytes);
            remaining-=bytes;offset+=bytes;
        }
    }
}
static int kernel_guards(void)
{
    io_service_t service=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("VMsvga2Accel"));
    io_connect_t connection=0;if(!service)return 0;
    IOReturn error=IOServiceOpen(service,mach_task_self(),2,&connection);IOObjectRelease(service);
    struct VIMGLTransfer open={1,0,0};struct VIMGLResult opened={0};size_t size=sizeof(opened);
    if(!error)error=IOConnectCallStructMethod(connection,kIOVM2DDirectGLTransfer,&open,sizeof(open),&opened,&size);
    if(error || !(opened.reserved&VIM_GL_RESULT_CAP_MASKED_SCATTER)) { if(connection)IOServiceClose(connection);return 0; }
    unsigned char pixels[2048];
    struct VIMGLScatterSpan spans[2]={{4,12},{260,12}};
    struct { struct VIMGLScatterRequest header;struct VIMGLScatterMask mask; } p={
        {3,2,0,512},{{(uintptr_t)pixels,160,128,0},4,0,(uintptr_t)spans}};
    int good=1;
    for(unsigned test=0;test<7;++test) {
        __typeof__(p) invalid=p;
        struct VIMGLScatterSpan invalid_spans[2];memcpy(invalid_spans,spans,sizeof(spans));
        invalid.mask.spans_address=(uintptr_t)invalid_spans;
        if(test==0)invalid_spans[1].offset=8; // overlap
        if(test==1)invalid_spans[1].bytes=0;
        if(test==2)invalid_spans[1].offset=508; // last span exceeds stream
        if(test==3)invalid.mask.reserved=1;
        if(test==4)invalid.mask.rows.address=UINT64_MAX-32;
        if(test==5)invalid.mask.rows.stride=VIM_GL_SCATTER_MAX_BYTES;
        if(test==6)invalid_spans[0].offset=1; // partial BGRA pixel
        struct VIMGLScatterResult result={0};size=sizeof(result);
        error=IOConnectCallStructMethod(connection,kIOVM2DDirectGLScatter,&invalid,sizeof(invalid),&result,&size);
        good=good && error==kIOReturnBadArgument;
    }
    struct VIMGLScatterResult result={0};size=sizeof(result);
    error=IOConnectCallStructMethod(connection,kIOVM2DDirectGLScatter,&p,sizeof(p),&result,&size);
    uint32_t token=result.token;good=good && !error && size==sizeof(result) && !result.count && token;
    if(token) {
        size=sizeof(result);
        error=IOConnectCallStructMethod(connection,kIOVM2DDirectGLScatter,&p,sizeof(p),&result,&size);
        good=good && error==kIOReturnExclusiveAccess;
        struct VIMGLScatterRequest release={2,0,token,0};size=sizeof(result);
        error=IOConnectCallStructMethod(connection,kIOVM2DDirectGLScatter,&release,sizeof(release),&result,&size);
        good=good && !error && !result.token && !result.count;
        struct VIMGLScatterRequest stale={1,0,token,512};size=sizeof(result);
        error=IOConnectCallStructMethod(connection,kIOVM2DDirectGLScatter,&stale,sizeof(stale),&result,&size);
        good=good && error==kIOReturnBadArgument;
    }
    IOServiceClose(connection);return good;
}
int main(void)
{
    enum { W=1280,H=880,C=4096 };
    void *shadow=calloc((size_t)W*H,4);void *context=DGCreateContext(NULL,NULL);
    if(!shadow || !context || !DGMakeCurrent(context,shadow,GL_UNSIGNED_BYTE,W,H))return 2;
    printf("renderer=%s masked_scatter=%d\n",glGetString(GL_RENDERER),DGSupportsMaskedScatterReads());fflush(stdout);
    if(!DGSupportsMaskedScatterReads())return 77;
    struct DGReadMaskRect rectangles[]={{0,0,173,H},{W-229,0,229,H},{100,300,W-200,111},{100,320,W-200,17}};
    struct DGReadMaskInterval scratch[4];struct DGReadSpan spans[C];
    int count=DGPlanReadMask(W,H,rectangles,4,scratch,spans,C);if(count<=0)return 3;
    size_t stride=W*4+64,allocation=stride*H+32;
    unsigned char *destination=malloc(allocation),*reference=malloc((size_t)W*H*4),*expected=malloc(allocation);
    if(!destination || !reference || !expected)return 4;
    int good=1;uint64_t valid=0;
    for(int i=0;i<count;++i)valid+=spans[i].bytes;
    glPixelStorei(GL_PACK_ROW_LENGTH,37);glPixelStorei(GL_PACK_SKIP_ROWS,3);glPixelTransferf(GL_RED_SCALE,0);
    double elapsed[2][2]={{0,0},{0,0}};
    for(int reverse=0;reverse<2;++reverse)for(int frame=0;frame<20;++frame) {
        int staged=frame&1;
        glClearColor(!!(frame&1),!!(frame&2),!!(frame&4),1);glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_SCISSOR_TEST);glScissor(130,333,701,177);glClearColor(.25f,.5f,.75f,1);
        glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
        memset(destination,0xa5,allocation);memset(expected,0xa5,allocation);
        double start=seconds();
        int read=staged?DGReadDrawableRect(reference,0,0,W,H):
            DGReadDrawableRectMasked(destination+16,stride,reverse,0,0,W,H,spans,(unsigned)count);
        if(staged && read)copy_rows(destination+16,stride,reference,spans,count,W,H,reverse);
        elapsed[staged][reverse]+=seconds()-start;
        int ordinary=DGReadDrawableRect(reference,0,0,W,H);good=good && read==1 && ordinary;
        if(ordinary)copy_rows(expected+16,stride,reference,spans,count,W,H,reverse);
        if(memcmp(destination,expected,allocation))good=0;
    }
    GLint row=0,skip=0;GLfloat scale=1;glGetIntegerv(GL_PACK_ROW_LENGTH,&row);
    glGetIntegerv(GL_PACK_SKIP_ROWS,&skip);glGetFloatv(GL_RED_SCALE,&scale);
    good=good && row==37 && skip==3 && scale==0;
    struct DGReadSpan invalid[]={{0,16},{8,16}};
    int rejected=DGReadDrawableRectMasked(destination,stride,0,0,0,W,H,invalid,2)==-1;
    printf("%s 40 changing GPU writes: both row orientations, overlapping rectangles, pitch/clip holes and guards\n",good?"PASS":"FAIL");
    printf("%s pre-submit invalid mask, %d spans, valid=%llu / stream=%u bytes\n",
        rejected?"PASS":"FAIL",count,(unsigned long long)valid,W*H*4);
    printf("interleaved masked receive %.3f / %.3f ms; staged + emulated clip copy %.3f / %.3f ms\n",
        elapsed[0][0]*100,elapsed[0][1]*100,elapsed[1][0]*100,elapsed[1][1]*100);fflush(stdout);
    int kernel=kernel_guards();printf("%s kernel metadata preflight, exclusive prepare, release and stale-token rejection\n",kernel?"PASS":"FAIL");
    GLenum error=glGetError();printf("%s final GL error=%x\n",error?"FAIL":"PASS",error);
    free(destination);free(reference);free(expected);DGDestroyContext(context);free(shadow);
    return !(good && rejected && kernel && !error);
}
