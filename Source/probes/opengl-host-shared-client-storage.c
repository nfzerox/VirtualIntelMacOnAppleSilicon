/* Local research: anonymous QEMU-like memory in one process, Intel GL in a
 * separately exec'd renderer. No file, pixel IPC, task port or inherited map.
 * Diagnostic GPU waits establish ownership; they are not frame pacing policy.
 * VIM_SHARED_MAPPING_BYTES / VIM_SHARED_USE_MMAP / VIM_SHARED_SINGLE_PIXEL
 * qualify a large private mmap with sparse CPU updates, as used by QEMU. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <servers/bootstrap.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/mman.h>
extern char **environ;
#ifdef VIM_SHARED_RENDER_TARGET
/* Match the observed auxiliary visible width, with aligned row padding.
 * This variant tests direct rasterization/blending, not a copy or clear. */
enum { Width=2472, Height=82, Prefix=65536, Rounds=8 };
enum { VisibleWidth=Width-4 };
#else
enum { Width=2560, Height=1600, Prefix=65536, Rounds=8 };
#endif
#define BACKING_BYTES ((mach_vm_size_t)Width*Height*4)
#ifndef VIM_SHARED_MAPPING_BYTES
#define VIM_SHARED_MAPPING_BYTES (Prefix+BACKING_BYTES)
#endif
#define MAPPING_BYTES (((mach_vm_size_t)VIM_SHARED_MAPPING_BYTES+16383)&~(mach_vm_size_t)16383)
struct Message {
    mach_msg_header_t h;
    mach_msg_body_t body;
    mach_msg_port_descriptor_t memory;
};
static void sendControl(mach_port_t destination,mach_port_t reply,int number)
{
    mach_msg_header_t h;memset(&h,0,sizeof(h));
    h.msgh_bits=MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,reply?MACH_MSG_TYPE_MAKE_SEND:0);
    h.msgh_remote_port=destination;h.msgh_local_port=reply;h.msgh_size=sizeof(h);h.msgh_id=number;
    assert(!mach_msg(&h,MACH_SEND_MSG|MACH_SEND_TIMEOUT,sizeof(h),0,0,10000,0));
}
static struct Message receiveMessage(mach_port_t port,int complex,int number)
{
    struct {struct Message m;mach_msg_max_trailer_t trailer;} packet;
    memset(&packet,0,sizeof(packet));
    assert(!mach_msg(&packet.m.h,MACH_RCV_MSG|MACH_RCV_TIMEOUT,0,sizeof(packet),port,10000,0));
    assert(packet.m.h.msgh_id==number);
    assert(!!(packet.m.h.msgh_bits&MACH_MSGH_BITS_COMPLEX)==complex);
    assert(packet.m.h.msgh_size==(complex?sizeof(packet.m):sizeof(mach_msg_header_t)));
    if(complex)assert(packet.m.body.msgh_descriptor_count==1 &&
        packet.m.memory.type==MACH_MSG_PORT_DESCRIPTOR &&
        packet.m.memory.disposition==MACH_MSG_TYPE_PORT_SEND);
    return packet.m;
}
static mach_port_t receivePort(void)
{
    mach_port_t port=0;
    assert(!mach_port_allocate(mach_task_self(),MACH_PORT_RIGHT_RECEIVE,&port));return port;
}
static uint32_t cpuColor(unsigned round)
{return 0xff000000u | ((round*19u+31u)<<16) | ((round*13u+23u)<<8) | (round*7u+11u);}
#ifdef VIM_SHARED_RENDER_TARGET
static int renderedPixel(int x,int y)
{return x>=17 && x<Width-17 && y>=9 && y<Height-9 && (x<Width/2-10 || x>=Width/2+10);}
static int blendedPixelMatches(uint32_t observed,uint32_t original)
{
    const unsigned source[]={192,128,64};
    if((observed>>24)!=(original>>24))return 0;
    for(unsigned channel=0;channel<3;++channel){
        int expected=(int)((source[channel]*128+((original>>(channel*8))&255)*127+127)/255);
        int actual=(int)((observed>>(channel*8))&255);
        if(abs(actual-expected)>1)return 0; // Native fixed-point blend rounding.
    }
    return 1;
}
static GLsync renderSharedTarget(void)
{
#if VIM_SHARED_RENDER_TARGET >= 2
    // Exact logical target width must constrain Clear too, not just viewport.
    // Deliberately no scissor: the row-padding sentinel must survive this.
    glDisable(GL_SCISSOR_TEST);glClearColor(.2f,.4f,.6f,1);glClear(GL_COLOR_BUFFER_BIT);
#endif
    glViewport(0,0,Width-4,Height);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,Width-4,0,Height,-1,1);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_FALSE);glColor4ub(64,128,192,128);
    glEnable(GL_SCISSOR_TEST);
    for(unsigned half=0;half<2;++half){
        int left=half?Width/2+10:17,right=half?Width-17:Width/2-10;
        glScissor(left,9,right-left,Height-18);
        glBegin(GL_QUADS);
        glVertex2i(0,0);glVertex2i(Width-4,0);glVertex2i(Width-4,Height);glVertex2i(0,Height);
        glEnd();
    }
    GLsync fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);assert(fence);
    glFlush();GLenum status;
    do{status=glClientWaitSync(fence,0,20000000);}while(status==GL_TIMEOUT_EXPIRED);
    assert(status==GL_ALREADY_SIGNALED || status==GL_CONDITION_SATISFIED);
    /* Parent must inspect ORIGINAL pages before any follow-up GL query,
     * readback, rebinding or object deletion can affect visibility. */
    return fence;
}
#endif
static int renderer(const char *service,int requireIntel)
{
    mach_port_t server=0,receive=receivePort();
    assert(!bootstrap_look_up(bootstrap_port,service,&server));
    sendControl(server,receive,1);
    struct Message m=receiveMessage(receive,1,2);
    mach_vm_address_t base=0;
    assert(!mach_vm_map(mach_task_self(),&base,MAPPING_BYTES,16383,VM_FLAGS_ANYWHERE,
        m.memory.name,0,FALSE,VM_PROT_READ|VM_PROT_WRITE,VM_PROT_READ|VM_PROT_WRITE,VM_INHERIT_NONE));
    mach_port_deallocate(mach_task_self(),m.memory.name);
    uint32_t *pixels=(uint32_t *)(uintptr_t)(base+Prefix);
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj context=NULL;GLint count=0;
    assert(!CGLChoosePixelFormat(attrs,&pf,&count) && pf);
    assert(!CGLCreateContext(pf,NULL,&context));CGLDestroyPixelFormat(pf);
    assert(!CGLSetCurrentContext(context));
    const char *name=(const char *)glGetString(GL_RENDERER);
    const char *vendor=(const char *)glGetString(GL_VENDOR);
    printf("child vendor=%s renderer=%s anonymous mapped bytes=%llu\n",vendor,name,(unsigned long long)MAPPING_BYTES);
    assert(!requireIntel || (name && vendor && strstr(name,"Intel") && strstr(vendor,"Intel")));
    const char *extensions=(const char *)glGetString(GL_EXTENSIONS);
    assert(extensions && strstr(extensions,"GL_APPLE_client_storage") && strstr(extensions,"GL_APPLE_texture_range"));
    GLuint texture=0,fbo=0;glGenTextures(1,&texture);glGenFramebuffersEXT(1,&fbo);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    glTextureRangeAPPLE(GL_TEXTURE_RECTANGLE_ARB,(GLsizei)BACKING_BYTES,pixels);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_STORAGE_HINT_APPLE,GL_STORAGE_SHARED_APPLE);
    glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE,GL_TRUE);
#if defined(VIM_SHARED_RENDER_TARGET) && VIM_SHARED_RENDER_TARGET >= 2
    glPixelStorei(GL_UNPACK_ROW_LENGTH,Width);
    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA8,VisibleWidth,Height,0,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,pixels);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
#else
    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA8,Width,Height,0,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,pixels);
#endif
    glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE,GL_FALSE);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,texture,0);
    assert(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
    GLuint sampleTexture=0,sampleFBO=0;
    glGenTextures(1,&sampleTexture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,sampleTexture);
    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA8,1,1,0,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,NULL);
    glGenFramebuffersEXT(1,&sampleFBO);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,sampleFBO);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,sampleTexture,0);
    assert(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glDisable(GL_DITHER);glDisable(GL_FRAMEBUFFER_SRGB_EXT);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    for(unsigned round=0;round<Rounds;++round){
        /* Parent writes CPU-newer pixels before releasing ownership to GL.
         * A readback is diagnostic here, before (not after) the GPU update. */
        receiveMessage(receive,0,10+(int)round);
        glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,fbo);glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,sampleFBO);
#if defined(VIM_SHARED_RENDER_TARGET) && VIM_SHARED_RENDER_TARGET >= 2
        glBlitFramebufferEXT(VisibleWidth-1,Height-1,VisibleWidth,Height,0,0,1,1,GL_COLOR_BUFFER_BIT,GL_NEAREST);
#else
        glBlitFramebufferEXT(Width-1,Height-1,Width,Height,0,0,1,1,GL_COLOR_BUFFER_BIT,GL_NEAREST);
#endif
        glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,sampleFBO);
        uint32_t value=0;glReadPixels(0,0,1,1,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,&value);
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
        assert(value==cpuColor(round));
#ifdef VIM_SHARED_RENDER_TARGET
        GLsync completed=renderSharedTarget();
#else
        glEnable(GL_SCISSOR_TEST);glScissor(3,7,251,197);
        glClearColor((round&1)?1:0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);glFinish();assert(!glGetError());
#endif
        /* Parent observes GPU-written pages through its ORIGINAL allocation.
         * No glReadPixels/glGetTexImage or CPU write follows the GPU clear. */
        sendControl(server,0,30+(int)round);
#ifdef VIM_SHARED_RENDER_TARGET
        receiveMessage(receive,0,60+(int)round);glDeleteSync(completed);assert(!glGetError());
        glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
#endif
    }
    receiveMessage(receive,0,50);
    glDeleteFramebuffersEXT(1,&sampleFBO);glDeleteTextures(1,&sampleTexture);
    glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&texture);glFinish();
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);
    assert(!mach_vm_deallocate(mach_task_self(),base,MAPPING_BYTES));
    mach_port_destroy(mach_task_self(),receive);mach_port_deallocate(mach_task_self(),server);
    return 0;
}
int main(int argc,char **argv)
{
    if(argc==4 && !strcmp(argv[1],"--child"))return renderer(argv[2],atoi(argv[3]));
    int requireIntel=argc==2 && !strcmp(argv[1],"--require-intel");
    if(argc!=1 && !requireIntel){fprintf(stderr,"Usage: %s [--require-intel]\n",argv[0]);return 2;}
    mach_vm_address_t base=0;
    assert(!mach_vm_map(mach_task_self(),&base,MAPPING_BYTES,16383,VM_FLAGS_ANYWHERE,
        MACH_PORT_NULL,0,FALSE,VM_PROT_READ|VM_PROT_WRITE,VM_PROT_READ|VM_PROT_WRITE,VM_INHERIT_NONE));
#ifdef VIM_SHARED_USE_MMAP
    assert(mmap((void *)(uintptr_t)base,MAPPING_BYTES,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANON|MAP_FIXED,-1,0)==(void *)(uintptr_t)base);
#endif
    memset((void *)(uintptr_t)base,0xa7,MAPPING_BYTES);
    mach_port_t memory=0;memory_object_size_t length=MAPPING_BYTES;
    assert(!mach_make_memory_entry_64(mach_task_self(),&length,base,
        VM_PROT_READ|VM_PROT_WRITE,&memory,MACH_PORT_NULL) && length==MAPPING_BYTES);
    mach_port_t receive=receivePort();
    assert(!mach_port_insert_right(mach_task_self(),receive,receive,MACH_MSG_TYPE_MAKE_SEND));
    char service[128];snprintf(service,sizeof(service),"com.virtualintelmac.test.client-memory.%d",getpid());
    assert(!bootstrap_register(bootstrap_port,service,receive));
    char *args[]={argv[0],"--child",service,requireIntel?"1":"0",NULL};pid_t child=0;
    assert(!posix_spawn(&child,argv[0],NULL,NULL,args,environ));
    struct Message hello=receiveMessage(receive,0,1);
    mach_port_t destination=hello.h.msgh_remote_port;assert(destination);
    struct Message reply;memset(&reply,0,sizeof(reply));
    reply.h.msgh_bits=MACH_MSGH_BITS_COMPLEX|MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,0);
    reply.h.msgh_remote_port=destination;reply.h.msgh_size=sizeof(reply);reply.h.msgh_id=2;
    reply.body.msgh_descriptor_count=1;reply.memory.name=memory;
    reply.memory.type=MACH_MSG_PORT_DESCRIPTOR;reply.memory.disposition=MACH_MSG_TYPE_COPY_SEND;
    assert(!mach_msg(&reply.h,MACH_SEND_MSG|MACH_SEND_TIMEOUT,sizeof(reply),0,0,10000,0));
    /* The renderer's received capability/map now retains the backing too. */
    mach_port_deallocate(mach_task_self(),memory);
    uint32_t *pixels=(uint32_t *)(uintptr_t)(base+Prefix);
    for(unsigned round=0;round<Rounds;++round){
#ifdef VIM_SHARED_SINGLE_PIXEL
        if(!round)for(size_t i=0;i<BACKING_BYTES/4;++i)pixels[i]=cpuColor(round);
        pixels[BACKING_BYTES/4-1]=cpuColor(round);
#else
        for(size_t i=0;i<BACKING_BYTES/4;++i)pixels[i]=cpuColor(round);
#endif
#if defined(VIM_SHARED_RENDER_TARGET) && VIM_SHARED_RENDER_TARGET >= 2
        for(int y=0;y<Height;++y)for(int x=VisibleWidth;x<Width;++x)pixels[(size_t)y*Width+x]=0xa7a7a7a7u;
#endif
        sendControl(destination,0,10+(int)round);receiveMessage(receive,0,30+(int)round);
        for(int y=0;y<Height;++y)for(int x=0;x<Width;++x){
#ifdef VIM_SHARED_RENDER_TARGET
            uint32_t actual=pixels[(size_t)y*Width+x],original=cpuColor(round);
#if VIM_SHARED_RENDER_TARGET >= 2
            original=x<VisibleWidth?0xff336699u:0xa7a7a7a7u;
#endif
            int good=renderedPixel(x,y)?blendedPixelMatches(actual,original):actual==original;
            if(!good)fprintf(stderr,"Direct shared target mismatch round=%u xy=%d,%d actual=%08x CPU=%08x\n",round,x,y,actual,original);
            assert(good);
#else
            uint32_t expected=x>=3 && x<254 && y>=7 && y<204?
                ((round&1)?0xffffff00u:0xff00ff00u):cpuColor(round);
#ifdef VIM_SHARED_SINGLE_PIXEL
            if(!(x>=3 && x<254 && y>=7 && y<204))expected=cpuColor(x==Width-1 && y==Height-1?round:0);
#endif
            assert(pixels[(size_t)y*Width+x]==expected);
#endif
        }
        const uint8_t *guard=(const uint8_t *)(uintptr_t)base;
        for(unsigned i=0;i<Prefix;++i)assert(guard[i]==0xa7);
        for(size_t i=Prefix+BACKING_BYTES;i<MAPPING_BYTES;++i)assert(guard[i]==0xa7);
#ifdef VIM_SHARED_RENDER_TARGET
        sendControl(destination,0,60+(int)round);
#endif
    }
    sendControl(destination,0,50);
    int status=0;assert(waitpid(child,&status,0)==child && WIFEXITED(status) && !WEXITSTATUS(status));
    mach_port_deallocate(mach_task_self(),destination);mach_port_destroy(mach_task_self(),receive);
    assert(!mach_vm_deallocate(mach_task_self(),base,MAPPING_BYTES));
#ifdef VIM_SHARED_RENDER_TARGET
    puts("PASS 8 direct shared render-target cycles: 2468x82 visible / 2472 pitch, blended quads, clip holes, alpha, row padding and guards preserved; original parent observes before follow-up GL queries; no pixel IPC or render readback");
#else
    puts("PASS 8 cross-process CPU/GPU ownership cycles: original anonymous pages, 2560x1600, partial writes and guards exact; no pixel IPC or render readback");
#endif
    return 0;
}
