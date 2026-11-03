/* Owned pitched imports: payload snapshot lifetime, orientation, guards,
 * state preservation and old-peer fallback, through either transport. */
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mach/mach_time.h>
#include "../PassthroughOpenGL/passthrough-opengl-surfaces.h"
extern void *DGCreateContext(const int *,void *);
extern GLboolean DGMakeCurrent(void *,void *,GLenum,GLsizei,GLsizei);
extern void DGDestroyContext(void *);
extern int DGReadDrawable(void *,int,int);
extern int DGImportDrawable(const void *,int,int,size_t,int);
extern int DGImportTransferStats(uint64_t *,uint64_t *);
static unsigned failures;
static void check(int good,const char *name) { printf("%s %s\n",good?"PASS":"FAIL",name);failures+=!good; }
static double seconds(void)
{ mach_timebase_info_data_t t;mach_timebase_info(&t);return mach_absolute_time()*(double)t.numer/t.denom/1e9; }
static void run(int width,int height,size_t padding,int flipped)
{
    size_t row=(size_t)width*4,stride=row+padding,span=(height-1)*stride+row,bytes=row*height;
    unsigned char *source=malloc(span+32),*expected=malloc(bytes),*out=malloc(bytes),*shadow=calloc(1,bytes);
    void *c=DGCreateContext(NULL,NULL);if(!source || !expected || !out || !shadow || !c)exit(2);
    check(DGMakeCurrent(c,shadow,GL_UNSIGNED_BYTE,width,height),"owned import drawable");
    memset(source,0xa5,span+32);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
        unsigned char pixel[4]={(unsigned char)x,(unsigned char)y,(unsigned char)(x^y),255};
        memcpy(source+y*stride+x*4,pixel,4);memcpy(expected+(flipped?height-1-y:y)*row+x*4,pixel,4);
    }
    GLuint pbo=0,texture=0;glGenBuffers(1,&pbo);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,pbo);
    glBufferData(GL_PIXEL_UNPACK_BUFFER,4096,NULL,GL_STREAM_DRAW);glBindBuffer(GL_PIXEL_PACK_BUFFER,pbo);
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,23);glPixelStorei(GL_UNPACK_SKIP_PIXELS,3);
    glPixelStorei(GL_UNPACK_SKIP_ROWS,2);glPixelStorei(GL_UNPACK_ALIGNMENT,8);
    glPixelTransferf(GL_RED_SCALE,0);glPixelTransferf(GL_ALPHA_BIAS,.25f);
    double start=seconds();int good=DGImportDrawable(source,width,height,stride,flipped);
    /* The caller may immediately reuse or unlock its backing. Even small
     * batched commands must have taken their snapshot before returning. */
    memset(source,0x5a,span);good=DGReadDrawable(out,width,height) && good;
    double elapsed=seconds()-start;
    uint64_t borrowed=0,copied=0;check(DGImportTransferStats(&borrowed,&copied),"numeric import route counters");
    uint64_t uploaded=0,source_span=0;int capable=DGRowUploadStats(&uploaded,&source_span);
    if(capable && bytes>=1024u*1024u && stride>=(size_t)width*6 && getenv("VIM_GL_ROW_UPLOAD"))
        check(uploaded==bytes && source_span==span,"native gather sends only canonical rows, not pitch holes");
    check(good && !memcmp(out,expected,bytes),"pitched import snapshot and exact pixel orientation");
    GLint buffer=0,length=0,skip=0,alignment=0,bound=0;GLfloat scale=1,bias=0;
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&buffer);glGetIntegerv(GL_UNPACK_ROW_LENGTH,&length);
    glGetIntegerv(GL_UNPACK_SKIP_ROWS,&skip);glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);
    glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&bound);glGetFloatv(GL_RED_SCALE,&scale);glGetFloatv(GL_ALPHA_BIAS,&bias);
    check(buffer==(GLint)pbo && length==23 && skip==2 && alignment==8 && bound==(GLint)texture && scale==0 && bias==.25f,
        "private import preserves application PBO, layout, texture and transfer state");
    int guard=1;for(size_t i=span;i<span+32;++i)guard&=source[i]==0xa5;
    check(guard && !glGetError(),"source canary and GL error state");
    check(!DGImportDrawable(source,width,height,row-1,flipped),"short source pitch rejected locally");
    printf("IMPORT width=%d height=%d padding=%lu flipped=%d span=%lu borrowed=%llu copied=%llu gathered=%llu roundtrip-ms=%.3f\n",width,height,(unsigned long)padding,flipped,(unsigned long)span,(unsigned long long)borrowed,(unsigned long long)copied,(unsigned long long)uploaded,elapsed*1000);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    glDeleteBuffers(1,&pbo);glDeleteTextures(1,&texture);DGDestroyContext(c);
    free(source);free(expected);free(out);free(shadow);
}
static void benchmark(int width,int height,size_t stride,int flipped)
{
    size_t bytes=(size_t)width*height*4,span=(height-1)*stride+width*4;
    unsigned char *source=calloc(1,span),*shadow=calloc(1,bytes),*out=malloc(bytes);
    void *c=DGCreateContext(NULL,NULL);if(!source || !shadow || !out || !c)exit(2);
    check(DGMakeCurrent(c,shadow,GL_UNSIGNED_BYTE,width,height),"benchmark drawable");
    double elapsed[2]={0,0};int good=1;
    for(int frame=0;frame<20;++frame) {
        int old=frame&1;
        if(old)unsetenv("VIM_GL_ROW_UPLOAD");else setenv("VIM_GL_ROW_UPLOAD","1",1);
        source[0]=(unsigned char)frame;
        double start=seconds();good=DGImportDrawable(source,width,height,stride,flipped) && good;
        elapsed[old]+=seconds()-start;
        // Drain/validate outside the CPU-submit timing, equally for both
        // modes. Same context/boot, interleaved paths, no GPU work overlap
        // carried into the next sample and no retained source pointer.
        good=DGReadDrawable(out,width,height) && good;
        if(out[(flipped?(size_t)(height-1)*width*4:0)]!=(unsigned char)frame)good=0;
    }
    unsetenv("VIM_GL_ROW_UPLOAD");
    uint64_t uploaded=0,source_bytes=0;int capable=DGRowUploadStats(&uploaded,&source_bytes);
    check(good && capable && uploaded==bytes*10 && source_bytes==span*10,"interleaved row-upload pixels and exact transfer counters");
    printf("UPLOAD width=%d height=%d stride=%lu reverse=%d bytes=%lu span=%lu gather=%.3f old=%.3f ms/submit\n",
        width,height,(unsigned long)stride,flipped,(unsigned long)bytes,(unsigned long)span,elapsed[0]*100,elapsed[1]*100);
    DGDestroyContext(c);free(source);free(shadow);free(out);
}
int main(void)
{
    for(int flipped=0;flipped<2;++flipped) {
        run(17,9,0,flipped);run(17,9,13,flipped);run(512,128,28,flipped);
        run(216,134,10240-216*4,flipped);run(1280,880,10240-1280*4,flipped);
    }
    if(getenv("VIM_IMPORT_VIEW_BENCHMARK")) {
        benchmark(1000,400,10240,0);benchmark(1600,880,10240,0);
        benchmark(1280,880,10240,0);benchmark(1280,880,10240,1);
    }
    printf("import view failures=%u\n",failures);return failures?1:0;
}
