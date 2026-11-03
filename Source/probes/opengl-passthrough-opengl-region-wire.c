/* Negotiated region publication across independent native GL connections. */
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <IOKit/IOKitLib.h>
#include <sys/sysctl.h>
#include <uuid/uuid.h>
#include "../vendor/source/VMsvga2/AC/UCMethods.h"
#include "../vendor/source/VMsvga2/vminclude/vim_gl_surface_service.h"
#include "../PassthroughOpenGL/passthrough-opengl-surfaces.h"
extern void *DGCreateContext(const int *,void *);
extern GLboolean DGMakeCurrent(void *,void *,GLenum,GLsizei,GLsizei);
extern void DGDestroyContext(void *);
static int pixel(int x,int y,unsigned r,unsigned g,unsigned b)
{
    unsigned char p[4]={0};glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    return !glGetError() && p[0]==r && p[1]==g && p[2]==b;
}
static int sealed_regions;
static int publish(uint32_t lease,const struct DGSurfaceRegion *regions,unsigned count,
    int flip,int back,uint64_t old,uint64_t next)
{
    return sealed_regions?DGPublishSealedSurfaceRegions(lease,regions,count,flip,back,old,next,0):
        DGPublishSurfaceRegions(lease,regions,count,flip,back,old,next);
}
int main(int argc,char **argv)
{
    sealed_regions=argc==2 && !strcmp(argv[1],"--sealed");
    if(argc!=1 && !sealed_regions)return 2;
    unsigned char source[32*32*4]={0},destination[64*64*4]={0};uint64_t object=0;
    arc4random_buf(&object,sizeof(object));if(!object)object=1;
    void *producer=DGCreateContext(NULL,NULL),*consumer=DGCreateContext(NULL,NULL);
    if(!producer || !consumer || !DGMakeCurrent(producer,source,GL_UNSIGNED_BYTE,32,32))return 2;
    /* Queued state must be observed without an explicit flush. The snapshot
     * must neither mutate state nor consume a pre-existing GL error. */
    GLuint pbo=0;glGenBuffers(1,&pbo);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,pbo);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,37);glPixelStorei(GL_UNPACK_ALIGNMENT,8);
    glPixelStorei(GL_UNPACK_SKIP_ROWS,2);glPixelStorei(GL_UNPACK_SKIP_PIXELS,3);
    glPixelStorei(GL_UNPACK_SWAP_BYTES,1);glPixelStorei(GL_UNPACK_LSB_FIRST,1);
    glEnable(0xffffffffu);
    int32_t snapshot[7]={0},expected[]={37,8,(int32_t)pbo,2,3,1,1};
    int snapshot_good=DGGetUnpackSnapshot(snapshot) && !memcmp(snapshot,expected,sizeof(snapshot));
    GLenum snapshot_error=glGetError();snapshot_good=snapshot_error==GL_INVALID_ENUM && snapshot_good;
    if(!snapshot_good)fprintf(stderr,"snapshot=%d,%d,%d,%d,%d,%d,%d expected-buffer=%u error=%x\n",
        snapshot[0],snapshot[1],snapshot[2],snapshot[3],snapshot[4],snapshot[5],snapshot[6],pbo,snapshot_error);
    const GLenum keys[]={GL_UNPACK_ROW_LENGTH,GL_UNPACK_ALIGNMENT,GL_PIXEL_UNPACK_BUFFER_BINDING,
        GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_PIXELS,GL_UNPACK_SWAP_BYTES,GL_UNPACK_LSB_FIRST};
    for(unsigned i=0;i<7;++i) { GLint value=-1;glGetIntegerv(keys[i],&value);snapshot_good=value==expected[i] && snapshot_good; }
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glDeleteBuffers(1,&pbo);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,0);glPixelStorei(GL_UNPACK_ALIGNMENT,4);
    glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
    glPixelStorei(GL_UNPACK_SWAP_BYTES,0);glPixelStorei(GL_UNPACK_LSB_FIRST,0);
    printf("%s batched unpack snapshot: queued state, PBO identity, state/error preservation\n",snapshot_good?"PASS":"FAIL");
    uint32_t out=DGAcquireSurface(object,1,64,64);
    int good=snapshot_good && out && DGSupportsSurfaceRegions() && DGUploadSurface(out,destination,64,64,256,1);
    glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
    struct DGSurfaceRegion region={0,0,16,16,16,16};
    good=publish(out,&region,1,0,0,1,2) && good;
    if(!DGMakeCurrent(consumer,destination,GL_UNSIGNED_BYTE,64,64))return 2;
    uint32_t in=DGAcquireSurface(object,1,64,64);
    good=in && DGAttachSurfaceDrawable(in) && good;
    good=pixel(16,16,0,0,255) && pixel(31,31,0,0,255) && pixel(15,16,0,0,0) && good;
    DGMakeCurrent(producer,source,GL_UNSIGNED_BYTE,32,32);
    glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    int stale=publish(out,&region,1,0,0,1,3);GLenum error=glGetError();
    good=!stale && error==GL_INVALID_OPERATION && good;
    region.destination_x=32;
    good=publish(out,&region,1,0,0,2,3) && good;
    DGMakeCurrent(consumer,destination,GL_UNSIGNED_BYTE,64,64);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);
    good=pixel(16,16,0,0,255) && pixel(32,16,255,0,0) && pixel(48,16,0,0,0) && good;
    uint64_t epoch=0;good=DGSurfaceEpoch(in,&epoch) && epoch==3 && good;
    printf("%s region wire: independent connections, preserved pixels, stale-writer rejection, epoch=%llu\n",good?"PASS":"FAIL",epoch);
    void *missing=DGCreateSurfaceReadHold(object,2,64,64);
    void *wrong_size=DGCreateSurfaceReadHold(object,1,32,64);
    good=!missing && !wrong_size && good;
    if(missing)DGReleaseSurfaceHold(missing);
    if(wrong_size)DGReleaseSurfaceHold(wrong_size);
    printf("%s service holds reject missing incarnation and mismatched dimensions\n",good?"PASS":"FAIL");
    DGMakeCurrent(producer,source,GL_UNSIGNED_BYTE,32,32);
    good=DGSealSurfaceVersion(out,3) && good;
    void *pinned=DGCreateSurfaceVersionHold(producer,object,1,64,64,3);
    glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);
    good=publish(out,&region,1,0,0,3,4) && DGSealSurfaceVersion(out,4) && good;
    void *latest=DGCreateSurfaceVersionHold(producer,object,1,64,64,4);
    good=pinned && latest && good;
    DGMakeCurrent(consumer,destination,GL_UNSIGNED_BYTE,64,64);
    GLuint version_texture=0;glGenTextures(1,&version_texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,version_texture);
    good=DGCopySurfaceVersionTexture(in,4,GL_RGBA8,64,64) && good;
    GLuint version_fbo=0;glGenFramebuffersEXT(1,&version_fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,version_fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,version_texture,0);
    glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
    unsigned char version_pixels[64*64*4]={0};glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,version_pixels);
    size_t red_at=(16*64+32)*4,blue_at=(16*64+16)*4;
    good=!memcmp(version_pixels+red_at,(unsigned char[]){0,255,0,255},4) &&
        !memcmp(version_pixels+blue_at,(unsigned char[]){0,0,255,255},4) && good;
    memset(version_pixels,0,sizeof(version_pixels));
    glTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB,0,0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,version_pixels);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&version_fbo);glDeleteTextures(1,&version_texture);
    printf("%s GPU-only immutable-version texture copy through wire\n",good?"PASS":"FAIL");
    void *hold=DGCreateSurfaceReadHold(object,1,64,64);good=hold && good;
    DGReleaseSurface(in);DGDestroyContext(consumer);
    DGMakeCurrent(producer,source,GL_UNSIGNED_BYTE,32,32);DGReleaseSurface(out);DGDestroyContext(producer);
    /* Both native contexts are gone. No GL entry point may be needed for
     * this read, and an unsuccessful read must not touch CPU storage. */
    unsigned char bytes[24];memset(bytes,0x5a,sizeof(bytes));
    unsigned char old_pixel[4]={0},new_pixel[4]={0};
    good=DGReadSurfaceHoldRect(pinned,3,32,16,1,1,old_pixel,4) &&
        DGReadSurfaceHoldRect(latest,4,32,16,1,1,new_pixel,4) && good;
    good=!memcmp(old_pixel,(unsigned char[]){0,0,255,255},4) &&
        !memcmp(new_pixel,(unsigned char[]){0,255,0,255},4) && good;
    DGReleaseSurfaceHold(pinned);DGReleaseSurfaceHold(latest);
    printf("%s pinned versions survive later publication, reseal and producer destruction\n",good?"PASS":"FAIL");
    int stale_read=DGReadSurfaceHoldRect(hold,2,16,16,2,2,bytes,12);
    good=!stale_read && good;
    for(unsigned i=0;i<sizeof(bytes);++i)good=bytes[i]==0x5a && good;
    good=DGReadSurfaceHoldRect(hold,4,16,16,2,2,bytes,12) && good;
    for(unsigned row=0;row<2;++row) {
        for(unsigned col=0;col<2;++col) {
            unsigned offset=row*12+col*4;
            good=bytes[offset]==255 && bytes[offset+1]==0 && bytes[offset+2]==0 && bytes[offset+3]==255 && good;
        }
        for(unsigned i=8;i<12;++i)good=bytes[row*12+i]==0x5a && good;
    }
    memset(bytes,0x5a,sizeof(bytes));
    good=!DGReadSurfaceHoldRect(hold,4,63,63,2,2,bytes,12) && good;
    for(unsigned i=0;i<sizeof(bytes);++i)good=bytes[i]==0x5a && good;
    printf("%s context-free CPU materialization: retained lifetime, version, bounds, pitched rows, failure preservation\n",good?"PASS":"FAIL");
    if(getenv("VIM_SURFACE_KERNEL_TEST")) {
        io_service_t device=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("VMsvga2Accel"));
        io_connect_t connection=0;IOReturn status=device?IOServiceOpen(device,mach_task_self(),2,&connection):kIOReturnNoDevice;
        if(device)IOObjectRelease(device);
        struct VIMGLServiceProbe request={0};char boot[64]={0};size_t length=sizeof(boot);uuid_t namespace={0};
        if(sysctlbyname("kern.bootsessionuuid",boot,&length,NULL,0) || uuid_parse(boot,namespace))status=kIOReturnError;
        memcpy(request.namespace_id,namespace,16);request.object=object;request.incarnation=1;request.epoch=4;
        request.width=request.height=64;request.x=request.y=16;
        unsigned char pixels[16]={0};length=sizeof(pixels);
        if(!status)status=IOConnectCallStructMethod(connection,kIOVM2DSurfaceServiceProbe,&request,sizeof(request),pixels,&length);
        good=!status && length==16 && good;
        for(unsigned i=0;i<4;++i)good=pixels[i*4]==255 && !pixels[i*4+1] && !pixels[i*4+2] && pixels[i*4+3]==255 && good;
        printf("%s kernel-owned materialization status=%x bytes=%lu\n",good?"PASS":"FAIL",status,(unsigned long)length);
        request.epoch=2;length=sizeof(pixels);
        IOReturn stale=connection?IOConnectCallStructMethod(connection,kIOVM2DSurfaceServiceProbe,&request,sizeof(request),pixels,&length):kIOReturnNoDevice;
        good=stale==kIOReturnNotReady && good;
        request.epoch=4;length=sizeof(pixels);
        status=connection?IOConnectCallStructMethod(connection,kIOVM2DSurfaceServiceProbe,&request,sizeof(request),pixels,&length):kIOReturnNoDevice;
        good=!status && good;
        printf("%s kernel stale read rejected without breaking subsequent valid read stale=%x next=%x\n",good?"PASS":"FAIL",stale,status);
        if(connection)IOServiceClose(connection);
    }
    DGReleaseSurfaceHold(hold);
    void *expired=DGCreateSurfaceReadHold(object,1,64,64);
    good=!expired && good;
    if(expired)DGReleaseSurfaceHold(expired);
    printf("%s released surface cannot be resurrected by a service hold\n",good?"PASS":"FAIL");
    return !good;
}
