/* Ordered copied-rectangle uploads must preserve unrelated queued GPU work
 * and hostile public pixel-transfer state. Uses only an owned drawable. */
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
static int pixel(const unsigned char *pixels,int x,int y,unsigned r,unsigned g,unsigned b)
{
    const unsigned char *p=pixels+((size_t)y*64+x)*4;
    return p[0]==b && p[1]==g && p[2]==r && p[3]==255;
}
static double seconds(void)
{ mach_timebase_info_data_t t;mach_timebase_info(&t);return mach_absolute_time()*(double)t.numer/t.denom/1e9; }
static int large_patch(int flipped,unsigned padding)
{
    enum {W=640,H=480,PW=416,PH=320,X=9,Y=17};
    size_t row=PW*4,stride=row+padding,span=(PH-1)*stride+row,bytes=PW*PH*4,full=W*H*4;
    unsigned char *source=malloc(span+32),*expected=calloc(1,full),*out=malloc(full),*shadow=calloc(1,full);
    void *context=DGCreateContext(NULL,NULL);if(!source||!expected||!out||!shadow||!context)exit(2);
    if(!DGMakeCurrent(context,shadow,GL_UNSIGNED_BYTE,W,H))exit(2);
    glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
    for(int y=0;y<H;y++)for(int x=0;x<W;x++) {expected[((size_t)y*W+x)*4]=255;expected[((size_t)y*W+x)*4+3]=255;}
    glEnable(GL_SCISSOR_TEST);glScissor(600,440,4,4);glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
    for(int y=440;y<444;y++)for(int x=600;x<604;x++) {unsigned char *p=expected+((size_t)y*W+x)*4;p[0]=0;p[2]=255;}
    memset(source,0xa5,span+32);
    for(int y=0;y<PH;y++)for(int x=0;x<PW;x++) {
        unsigned char p[4]={(unsigned char)(x^y),(unsigned char)y,(unsigned char)x,255};
        memcpy(source+y*stride+x*4,p,4);
        memcpy(expected+((size_t)(Y+(flipped?PH-1-y:y))*W+X+x)*4,p,4);
    }
    uint64_t old=0,old_span=0,uploaded=0,source_bytes=0;int capable=DGRowUploadStats(&old,&old_span);
    int good=DGImportDrawablePatch(source,X,Y,PW,PH,(unsigned)stride,flipped);
    memset(source,0x5a,span); // No borrowed pointer may outlive return.
    good=DGReadDrawable(out,W,H)&&good&&!memcmp(expected,out,full);
    DGRowUploadStats(&uploaded,&source_bytes);
    if(capable&&!getenv("VIM_GL_DISABLE_PATCH_GATHER")&&(flipped||padding))
        good=good&&uploaded-old==bytes&&source_bytes-old_span==span;
    for(size_t i=span;i<span+32;i++)good=good&&source[i]==0xa5;
    good=good&&!glGetError();
    printf("%s large patch flip=%d padding=%u exact pixels, outside writes, immediate source reuse, canary; gathered=%llu\n",
        good?"PASS":"FAIL",flipped,padding,(unsigned long long)(uploaded-old));
    if(getenv("VIM_PATCH_BENCHMARK")&&padding) {
        double elapsed[2]={0,0};
        for(unsigned i=0;i<20;i++) {
            unsigned old_route=i&1;
            if(old_route)setenv("VIM_GL_DISABLE_PATCH_GATHER","1",1);else unsetenv("VIM_GL_DISABLE_PATCH_GATHER");
            source[0]=(unsigned char)i;
            double start=seconds();good=DGImportDrawablePatch(source,X,Y,PW,PH,(unsigned)stride,flipped)&&good;
            elapsed[old_route]+=seconds()-start;
            good=DGReadDrawable(out,W,H)&&good;
            good=good&&out[((size_t)(Y+(flipped?PH-1:0))*W+X)*4]==i;
        }
        unsetenv("VIM_GL_DISABLE_PATCH_GATHER");
        printf("PATCH BENCH flip=%d payload=%lu span=%lu native-ms=%.3f packed-ms=%.3f pixels=%d\n",flipped,
            (unsigned long)bytes,(unsigned long)span,elapsed[0]*100,elapsed[1]*100,good);
    }
    DGDestroyContext(context);free(source);free(expected);free(out);free(shadow);return good;
}
int main(void)
{
    unsigned char shadow[64*48*4]={0},out[sizeof(shadow)]={0},patch[4*48];
    void *context=DGCreateContext(NULL,NULL);
    if(!context || !DGMakeCurrent(context,shadow,GL_UNSIGNED_BYTE,64,48))return 2;
    glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);glScissor(30,30,4,4);
    glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
    memset(patch,0xa5,sizeof(patch));
    for(unsigned y=0;y<4;++y)for(unsigned x=0;x<8;++x) {
        unsigned char *p=patch+y*48+x*4;p[0]=0;p[1]=y<2?255:0;p[2]=y<2?0:255;p[3]=255;
    }
    GLuint pbo=0,texture=0;glGenBuffers(1,&pbo);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,pbo);glBufferData(GL_PIXEL_UNPACK_BUFFER,4096,NULL,GL_STREAM_DRAW);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,pbo);
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,23);glPixelStorei(GL_UNPACK_SKIP_PIXELS,3);
    glPixelStorei(GL_UNPACK_SKIP_ROWS,2);glPixelStorei(GL_UNPACK_ALIGNMENT,8);
    glPixelStorei(GL_UNPACK_SWAP_BYTES,GL_TRUE);glPixelStorei(GL_UNPACK_LSB_FIRST,GL_TRUE);
    glPixelTransferf(GL_RED_SCALE,0);glPixelTransferf(GL_ALPHA_BIAS,.25f);
    int good=DGImportDrawablePatch(patch,5,7,8,4,48,1) && DGReadDrawable(out,64,48);
    GLint binding=0,row=0,skip=0,swap=0,tex=0;GLfloat scale=1,bias=0;
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&binding);glGetIntegerv(GL_UNPACK_ROW_LENGTH,&row);
    glGetIntegerv(GL_UNPACK_SKIP_ROWS,&skip);glGetIntegerv(GL_UNPACK_SWAP_BYTES,&swap);
    glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&tex);
    glGetFloatv(GL_RED_SCALE,&scale);glGetFloatv(GL_ALPHA_BIAS,&bias);
    good=good && binding==(GLint)pbo && row==23 && skip==2 && swap && tex==(GLint)texture && scale==0 && bias==.25f;
    int pixels=pixel(out,6,7,255,0,0) && pixel(out,6,10,0,255,0) &&
        pixel(out,32,32,255,0,0) && pixel(out,4,7,0,0,255) && pixel(out,13,7,0,0,255);
    printf("%s rectangle upload orientation, untouched pixels and prior GPU writes\n",pixels?"PASS":"FAIL");
    printf("%s rectangle upload preserves pixel-transfer, PBO and texture state\n",good?"PASS":"FAIL");
    int bounds=!DGImportDrawablePatch(patch,-1,0,8,4,48,0) &&
        !DGImportDrawablePatch(patch,63,0,8,4,48,0) && !DGImportDrawablePatch(patch,0,0,8,4,16,0);
    GLenum error=glGetError();printf("%s invalid rectangle bounds/stride rejected; GL=%x\n",bounds&&!error?"PASS":"FAIL",error);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    glDeleteBuffers(1,&pbo);glDeleteTextures(1,&texture);DGDestroyContext(context);
    int large=1;
    for(int flipped=0;flipped<2;flipped++) {
        large=large_patch(flipped,0)&&large;
        large=large_patch(flipped,2432)&&large;
    }
    return !(good && pixels && bounds && !error && large);
}
