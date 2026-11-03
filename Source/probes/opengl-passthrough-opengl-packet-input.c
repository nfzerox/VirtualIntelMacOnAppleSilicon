/* Exercise actual packet ownership as well as GPU-visible GL results. The
 * embedded client is intentional: the system guest renderer stays unchanged
 * while this candidate tests its private static packet-builder invariants. */
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#include <sys/mman.h>
static unsigned failures;
static void check(int good,const char *name)
{ printf("%s %s\n",good?"PASS":"FAIL",name);fflush(stdout);failures+=!good; }
static size_t allocation_size(size_t bytes)
{ size_t page=(size_t)getpagesize();return (bytes+page-1)/page*page; }
static unsigned char *pages(size_t bytes)
{
    void *p=mmap(NULL,allocation_size(bytes),PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    if(p==MAP_FAILED)exit(2);return p;
}
static void builder(void)
{
    const size_t bytes=65536;unsigned char *source=pages(bytes);memset(source,0x73,bytes);
    if(mprotect(source,allocation_size(bytes),PROT_READ))exit(2);
    struct DGPacket packet=DGPacketNew(NULL,1);
    DGPacketPointer(&packet,0,source,bytes,0);
    int borrowing=!getenv("VIM_GL_DISABLE_PACKET_BORROW");
    check(!packet.failed && packet.borrowed==borrowing && packet.message.args[0]==1,
        "large input packet borrows only when enabled");
    DGPacketPointer(&packet,1,"end",4,0);
    check(!packet.failed && !packet.borrowed && packet.data!=source &&
        packet.message.args[1]==bytes+1 && packet.message.bytes==bytes+4 &&
        packet.data[0]==0x73 && packet.data[bytes-1]==0x73 && !memcmp(packet.data+bytes,"end",4),
        "second span promotes read-only input before append");
    DGPacketFree(&packet);
    packet=DGPacketNew(NULL,1);DGPacketPointer(&packet,0,source,bytes,0);
    check(DGPacketOwn(&packet,bytes) && !packet.borrowed && packet.data!=source &&
        !memcmp(packet.data,source,bytes),"reply promotion owns an independent writable snapshot");
    memset(packet.data,0x19,bytes);DGPacketFree(&packet);
    check(source[0]==0x73 && source[bytes-1]==0x73,"promotion never writes original read-only storage");
    packet=DGPacketNew(NULL,1);DGPacketPointer(&packet,0,source,bytes,0);
    DGPacketPointer(&packet,1,source,SIZE_MAX,0);
    check(packet.failed,"invalid later span rejects borrowed packet");DGPacketFree(&packet);
    check(source[0]==0x73,"failed/free borrowed packet never frees caller memory");
    munmap(source,allocation_size(bytes));
}
static void shared_patch_fallback(void)
{
    struct DGConnection connection={.fd=-1};struct DGContext context={.connection=&connection,.width=16,.height=16};
    pthread_mutex_init(&connection.lock,NULL);
    struct DGContext *previous=dg_current;dg_current=&context;
    struct VIMSurfaceVRAM backing={0};backing.version=VIM_SURFACE_VRAM_VERSION;
    backing.width=backing.height=16;backing.row_bytes=64;backing.length=4096;
    backing.state=(struct VIMSurfaceState){.version=VIM_SURFACE_STATE_VERSION,.flags=VIM_SURFACE_STATE_CONTEXT_LOCKED,
        .object=17,.incarnation=3,.content_epoch=4};
    struct DGVRAMRect rect={1,2,3,4};
    connection.capabilities=DG_CAP_VRAM_DRAWABLES;
    check(DGImportSharedDrawableRects(&backing,0,0,16,16,0,&rect,1)==-1 &&
        !context.error && !connection.dead && !connection.used,"old peer rejects shared patch locally without I/O/error");
    connection.capabilities|=DG_CAP_VRAM_PATCHES;
    check(DGImportSharedDrawableRects(&backing,0,0,16,16,0,NULL,0)==-1 && !context.error,
        "empty shared patch is not a full-view import");
    rect.x=-1;
    check(DGImportSharedDrawableRects(&backing,0,0,16,16,0,&rect,1)==-1 && !connection.dead,
        "malformed shared patch is rejected before transport");rect.x=1;
    backing.version=VIM_SURFACE_RAM_VERSION;
    check(DGImportSharedDrawableRects(&backing,0,0,16,16,0,&rect,1)==-1 && !connection.dead,
        "RAM patch requires its separate negotiated mapping capability");
    check(DGImportIOSurfaceTexture(GL_RGB8,1,1,"xxxx",4)==-1 && !connection.used && !context.error,
        "old peer rejects native IOSurface import locally without I/O/error");
    connection.capabilities|=DG_CAP_IOSURFACE_TEXTURE;
    check(DGImportIOSurfaceTexture(GL_RGB8,-1,1,"xxxx",4)==0 && context.error==GL_INVALID_VALUE && !connection.dead,
        "malformed native IOSurface import is rejected before transport");
    context.error=0;connection.capabilities|=DG_CAP_APPLE_VIDEO_TEXTURES;
    check(DGImportIOSurfaceTextureFormat(GL_RGB,0x85b9,0x85ba,2,1,"xxxx",4)==-1 &&
        !context.error&&!connection.used&&!connection.dead,
        "old peer keeps ordinary packed-YCbCr fallback without sending new opcode");
    check(DGImportIOSurfaceTextureFormat(GL_RGB,0x8a1f,0x85ba,2,1,"xxxx",4)==0 &&
        context.error==GL_INVALID_OPERATION&&!connection.used&&!connection.dead,
        "old peer rejects raw RGB422 rather than interpreting it as BGRA or luminance-alpha");
    dg_current=previous;pthread_mutex_destroy(&connection.lock);
}
static void native_iosurface_input(void)
{
    enum {W=64,H=64,Bytes=W*H*4};unsigned char *source=pages(Bytes);
    for(unsigned i=0;i<Bytes;i+=4){source[i]=191;source[i+1]=73;source[i+2]=11;source[i+3]=37;}
    GLuint texture=0,output=0,fbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    if(mprotect(source,allocation_size(Bytes),PROT_READ))exit(2);
    check(DGImportIOSurfaceTexture(GL_RGB8,W,H,source,W*4)==1 && !glGetError(),
        "native RGB IOSurface input accepts read-only snapshot");
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,texture,0);
    unsigned char p[4]={0};glReadPixels(17,23,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    check(!glGetError() && p[0]==11 && p[1]==73 && p[2]==191 && p[3]==37,
        "native RGB IOSurface attachment retains backing alpha");
    glGenTextures(1,&output);glBindTexture(GL_TEXTURE_2D,output);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,W,H,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,output,0);
    glViewport(0,0,W,H);glEnable(GL_TEXTURE_RECTANGLE_ARB);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    glBegin(GL_QUADS);glTexCoord2i(0,0);glVertex2f(-1,-1);glTexCoord2i(W,0);glVertex2f(1,-1);
    glTexCoord2i(W,H);glVertex2f(1,1);glTexCoord2i(0,H);glVertex2f(-1,1);glEnd();
    glReadPixels(17,23,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    check(!glGetError() && p[0]==11 && p[1]==73 && p[2]==191 && p[3]==255,
        "same native RGB IOSurface samples opaque alpha");
    glDisable(GL_TEXTURE_RECTANGLE_ARB);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);
    glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&texture);glDeleteTextures(1,&output);
    munmap(source,allocation_size(Bytes));
}
static void texture_input(void)
{
    enum {W=256,H=128,ROW=288,SKIP_X=5,SKIP_Y=2};
    size_t stride=ROW*4,span=(SKIP_Y+H-1)*stride+(SKIP_X+W)*4,bytes=W*H*4;
    unsigned char *source=pages(span),*expected=malloc(bytes),*out=malloc(bytes);
    if(!expected||!out)exit(2);memset(source,0xa5,span);
    for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W;x++) {
        unsigned char p[4]={(unsigned char)x,(unsigned char)y,(unsigned char)(x^y),255};
        memcpy(source+(SKIP_Y+y)*stride+(SKIP_X+x)*4,p,4);memcpy(expected+(y*W+x)*4,p,4);
    }
    if(mprotect(source,allocation_size(span),PROT_READ))exit(2);
    GLuint texture=0,fbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,ROW);glPixelStorei(GL_UNPACK_SKIP_ROWS,SKIP_Y);glPixelStorei(GL_UNPACK_SKIP_PIXELS,SKIP_X);
    uint64_t before=0,promoted=0,after=0,promoted_after=0;DGBorrowedPacketStats(&before,&promoted);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,W,H,0,GL_RGBA,GL_UNSIGNED_BYTE,source);
    // The application may reuse memory before the subsequent GPU execution.
    if(mprotect(source,allocation_size(span),PROT_READ|PROT_WRITE))exit(2);memset(source,0x5a,span);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,0);glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,texture,0);
    check(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT,"uploaded texture is an actual GPU framebuffer");
    glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,out);
    DGBorrowedPacketStats(&after,&promoted_after);
    check(!memcmp(out,expected,bytes)&&!glGetError(),"pitched/skipped texture input snapshot survives immediate reuse");
    check(getenv("VIM_GL_DISABLE_PACKET_BORROW")?after==before:after-before==span,
        "numeric route confirms actual borrowed texture payload");
    check(getenv("VIM_GL_DISABLE_PACKET_BORROW")?promoted_after==promoted:promoted_after-promoted==bytes,
        "large seeded ReadPixels reply is promoted, not received into borrowed input");
    for(unsigned i=0;i<bytes;i+=4){source[i]=1;source[i+1]=2;source[i+2]=3;source[i+3]=255;}
    if(mprotect(source,allocation_size(span),PROT_READ))exit(2);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,source);
    if(mprotect(source,allocation_size(span),PROT_READ|PROT_WRITE))exit(2);memset(source,0xe7,bytes);
    glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,out);
    int good=!glGetError();for(size_t i=0;i<bytes;i+=4)if(out[i]!=1||out[i+1]!=2||out[i+2]!=3||out[i+3]!=255){good=0;break;}
    check(good,"large read-only TexSubImage input snapshot is independent of later caller writes");
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&texture);
    free(expected);free(out);munmap(source,allocation_size(span));
}
static void buffer_input(void)
{
    const size_t bytes=131072;unsigned char *source=pages(bytes),*out=malloc(bytes);if(!out)exit(2);
    for(size_t i=0;i<bytes;i++)source[i]=(unsigned char)(i^(i>>8));
    if(mprotect(source,allocation_size(bytes),PROT_READ))exit(2);
    GLuint buffer=0;glGenBuffers(1,&buffer);glBindBuffer(GL_ARRAY_BUFFER,buffer);
    glBufferData(GL_ARRAY_BUFFER,bytes,source,GL_STATIC_DRAW);
    if(mprotect(source,allocation_size(bytes),PROT_READ|PROT_WRITE))exit(2);memset(source,0x33,bytes);
    glGetBufferSubData(GL_ARRAY_BUFFER,0,bytes,out);
    int good=!glGetError();for(size_t i=0;i<bytes;i++)if(out[i]!=(unsigned char)(i^(i>>8))){good=0;break;}
    check(good,"large BufferData input snapshot survives immediate reuse");
    // Synchronous INPUT_ONLY commands must not receive an echo into const
    // input: deleting a bound PBO takes this exact route in the real client.
    glBindBuffer(GL_PIXEL_PACK_BUFFER,buffer);memset(source,0,bytes);((GLuint *)source)[0]=buffer;
    if(mprotect(source,allocation_size(bytes),PROT_READ))exit(2);
    uint64_t before=0,promoted=0,after=0,promoted_after=0;DGBorrowedPacketStats(&before,&promoted);
    glDeleteBuffers((GLsizei)(bytes/sizeof(GLuint)),(const GLuint *)source);
    DGBorrowedPacketStats(&after,&promoted_after);GLint binding=-1;
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&binding);
    check(binding==0&&!glGetError()&&promoted_after==promoted &&
        (getenv("VIM_GL_DISABLE_PACKET_BORROW")?after==before:after-before==bytes),
        "synchronous INPUT_ONLY PBO deletion preserves read-only input and returns binding state");
    free(out);munmap(source,allocation_size(bytes));
}
static void retained_upload_input(void)
{
    enum {W=256,H=128};
    uint64_t object=0;arc4random_buf(&object,sizeof(object));if(!object)object=1;
    uint32_t lease=DGAcquireSurface(object,1,W,H);
    GLuint texture=0,fbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    check(lease && DGBindSurface(lease,GL_RGBA8),"retained upload acquires independent storage");
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,texture,0);
    int disabled=getenv("VIM_GL_DISABLE_PACKET_BORROW")!=NULL;
    for(unsigned mode=0;mode<3;mode++) {
        size_t stride=(W+(mode==2?16:0))*4,bytes=stride*H;
        unsigned char *source=pages(bytes);memset(source,0xee,bytes);
        for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W;x++) {
            unsigned char *p=source+y*stride+x*4;
            p[0]=x;p[1]=y;p[2]=31+mode;p[3]=255;
        }
        if(mode==1)setenv("VIM_GL_DISABLE_PACKET_BORROW","1",1);
        else if(!disabled)unsetenv("VIM_GL_DISABLE_PACKET_BORROW");
        if(mprotect(source,allocation_size(bytes),PROT_READ))exit(2);
        uint64_t before=0,promoted=0,after=0,unused=0;DGBorrowedPacketStats(&before,&promoted);
        int good=DGUploadSurface(lease,source,W,H,(unsigned)stride,mode+1);
        DGBorrowedPacketStats(&after,&unused);
        check(good && after-before==(!disabled && mode==0 ? W*H*4 : 0),
            "retained upload borrows only tight synchronous input when enabled");
        if(mprotect(source,allocation_size(bytes),PROT_READ|PROT_WRITE))exit(2);
        memset(source,0,bytes); // Caller may reuse storage immediately after the reply.
        unsigned char pixel[4]={0};glReadPixels(17,23,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        check(!glGetError() && pixel[0]==31+mode && pixel[1]==23 && pixel[2]==17 && pixel[3]==255,
            "retained upload consumes read-only BGRA rows before caller reuse");
        munmap(source,allocation_size(bytes));
    }
    if(disabled)setenv("VIM_GL_DISABLE_PACKET_BORROW","1",1);else unsetenv("VIM_GL_DISABLE_PACKET_BORROW");
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&texture);
    check(DGReleaseSurface(lease),"retained input storage releases cleanly");
}
static void benchmark(unsigned width,unsigned height)
{
    size_t bytes=(size_t)width*height*4;unsigned char *source=pages(bytes),*out=malloc(bytes);
    if(!out)exit(2);memset(source,0x7f,bytes);
    GLuint texture=0,fbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,texture,0);
    glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,out); // Settle setup outside timing.
    double elapsed[2]={0,0};int good=1;mach_timebase_info_data_t clock;mach_timebase_info(&clock);
    for(unsigned frame=0;frame<20;frame++) {
        unsigned old=frame&1;
        if(old)setenv("VIM_GL_DISABLE_PACKET_BORROW","1",1);else unsetenv("VIM_GL_DISABLE_PACKET_BORROW");
        source[0]=(unsigned char)frame;
        uint64_t start=mach_absolute_time();
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,source);
        elapsed[old]+=(mach_absolute_time()-start)*(double)clock.numer/clock.denom/1e6;
        // Validate after a complete GPU round trip, equally for both modes.
        // There is no outstanding upload from a prior trial in the next clock.
        glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,out);
        if(out[0]!=frame||glGetError())good=0;
    }
    unsetenv("VIM_GL_DISABLE_PACKET_BORROW");
    printf("PACKET BENCH size=%ux%u bytes=%lu borrowed-ms=%.3f copied-ms=%.3f pixels=%d\n",
        width,height,(unsigned long)bytes,elapsed[0]/10,elapsed[1]/10,good);
    check(good,"interleaved upload benchmark validates actual GPU content");
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&texture);
    munmap(source,allocation_size(bytes));free(out);
}
int main(int argc,char **argv)
{
    builder();shared_patch_fallback();
    if(argc==2 && !strcmp(argv[1],"--builder-only"))return failures?1:0;
    int nativeOnly=argc==2 && !strcmp(argv[1],"--native-io-only");
    int retainedOnly=argc==2 && !strcmp(argv[1],"--retained-upload-only");
    if(argc!=1 && !nativeOnly && !retainedOnly){fprintf(stderr,"Usage: %s [--builder-only|--native-io-only|--retained-upload-only]\n",argv[0]);return 2;}
    unsigned char *shadow=calloc(256*192,4);
    void *context=DGCreateContext(NULL,NULL);
    if(!shadow||!context||!DGMakeCurrent(context,shadow,GL_UNSIGNED_BYTE,256,192))return 2;
    if(nativeOnly)native_iosurface_input();else if(retainedOnly)retained_upload_input();
    else {texture_input();buffer_input();retained_upload_input();}
    if(getenv("VIM_PACKET_BENCHMARK")){benchmark(256,128);benchmark(1024,768);}
    DGDestroyContext(context);free(shadow);return failures?1:0;
}
