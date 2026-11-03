/* Client packet -> real independent host GL contexts, without touching a VM. */
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../PassthroughOpenGL/passthrough-opengl-surfaces.h"
extern void *DGCreateContext(const int *,void *);
extern GLboolean DGMakeCurrent(void *,void *,GLenum,GLsizei,GLsizei);
extern void DGDestroyContext(void *);
static int check(int x,int y,unsigned r,unsigned g)
{
    unsigned char bytes[4]={0};glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,bytes);
    GLenum error=glGetError();int good=!error && bytes[0]==r && bytes[1]==g && bytes[2]==17 && bytes[3]==255;
    if(!good)fprintf(stderr,"pixel %d,%d expected=%u,%u,17,255 actual=%u,%u,%u,%u error=%x\n",x,y,r,g,bytes[0],bytes[1],bytes[2],bytes[3],error);
    return good;
}
int main(void)
{
    unsigned char seed[64*64*4];uint64_t object=0;arc4random_buf(&object,sizeof(object));if(!object)object=1;
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
        unsigned i=(y*64+x)*4;seed[i]=17;seed[i+1]=y;seed[i+2]=x;seed[i+3]=255; // BGRA upload.
    }
    void *a=DGCreateContext(NULL,NULL),*b=DGCreateContext(NULL,NULL);
    if(!a || !b || !DGMakeCurrent(a,seed,GL_UNSIGNED_BYTE,64,64))return 2;
    uint32_t from=DGAcquireSurface(object,1,64,64);
    int good=from && DGSupportsSurfaceCopies() && DGUploadSurface(from,seed,64,64,256,7);
    printf("seed=%d lease=%u capability=%d\n",good,from,DGSupportsSurfaceCopies());
    struct DGSurfaceRegion rects[]={{0,0,48,32,16,0},{48,0,16,32,0,0}};
    good=DGCopySurfaceRegions(from,rects,2,7,8) && good;
    printf("copy=%d error=%x\n",good,glGetError());
    /* GA copies interleave with a local view's earlier unflushed drawing.
     * Patching it must preserve all unrelated pixels and the canonical epoch. */
    glClearColor(0,1,1,1);glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);glScissor(40,40,8,8);glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    good=DGSupportsSurfacePatchImport() && DGImportSurfacePatch(from,8,0,0,16,16,16,16,0) &&
        check(16,16,48,0) && check(31,31,63,15) && good;
    good=DGImportSurfacePatch(from,8,0,0,16,16,0,32,1) && check(0,32,48,15) && check(0,47,48,0) && good;
    unsigned char untouched[4]={0},earlier[4]={0};
    glReadPixels(15,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,untouched);glReadPixels(40,40,1,1,GL_RGBA,GL_UNSIGNED_BYTE,earlier);
    good=!memcmp(untouched,(unsigned char[]){0,255,255,255},4) &&
        !memcmp(earlier,(unsigned char[]){255,0,0,255},4) && glIsEnabled(GL_SCISSOR_TEST) && good;
    int bad_patch=DGImportSurfacePatch(from,8,0,0,16,16,60,0,0);
    good=!bad_patch && glGetError()==GL_INVALID_VALUE && good;glDisable(GL_SCISSOR_TEST);
    printf("%s GPU patch import: translated/flipped view, earlier GL drawing, holes, bounds, caller state\n",good?"PASS":"FAIL");
    if(!DGMakeCurrent(b,seed,GL_UNSIGNED_BYTE,64,64))return 2;
    uint32_t to=DGAcquireSurface(object,1,64,64);uint64_t epoch=0;
    good=to && DGSurfaceEpoch(to,&epoch) && epoch==8 && DGAttachSurfaceDrawable(to) && good;
    printf("consumer=%d lease=%u epoch=%llu error=%x\n",good,to,epoch,glGetError());
    good=check(0,0,48,0) && check(16,0,0,0) && check(63,31,47,31) && check(32,63,32,63) && good;
    DGMakeCurrent(a,seed,GL_UNSIGNED_BYTE,64,64);
    int stale=DGCopySurfaceRegions(from,rects,2,7,9);GLenum error=glGetError();
    printf("stale=%d error=%x\n",stale,error);
    good=!stale && error==GL_INVALID_OPERATION && good;
    DGReleaseSurface(from);DGDestroyContext(a);DGMakeCurrent(b,seed,GL_UNSIGNED_BYTE,64,64);
    good=DGSurfaceEpoch(to,&epoch) && epoch==8 && check(0,0,48,0) && good;
    DGReleaseSurface(to);DGDestroyContext(b);
    printf("%s GPU copy wire: negotiated capability, overlap, preserved pixels, cross-context visibility, stale epoch, lifetime\n",good?"PASS":"FAIL");
    return !good;
}
