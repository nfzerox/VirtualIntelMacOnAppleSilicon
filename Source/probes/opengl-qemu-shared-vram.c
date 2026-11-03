/* Paused device-only research, not a guest boot: prove that the exported
 * allocation really is QEMU's VMware VRAM, including GPU/CPU coherence.
 * qtest reads the PCI BAR independently; no replacement file-backed RAM. */
#include "../vendor/qemu/hw/display/vim-vram-memory.h"
#include "../vendor/source/VMsvga2/vminclude/vim_passthrough_opengl.h"
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <spawn.h>
#include <signal.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <mach/mach_time.h>
extern char **environ;
static pid_t qemu;
static char scratch[128],socketPath[160];
static FILE *qtest;
static int testRAM;
static int useDoorbell;
static unsigned aliasPages;
static uint64_t pageAddress(unsigned page)
{return ((page&1)?0x100200000ull:0x200000ull)+(uint64_t)page*4096;}
static unsigned long long guestAddress(unsigned offset)
{
    return testRAM?pageAddress(aliasPages-1-offset/4096)+(offset&4095):0xf0000000ull+offset;
}
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void cleanup(void)
{
    if(qtest)fclose(qtest);
    if(qemu>0){kill(qemu,SIGTERM);waitpid(qemu,NULL,0);}
    if(*socketPath)unlink(socketPath);if(*scratch)rmdir(scratch);
}
static unsigned command(const char *format,...)
{
    va_list args;va_start(args,format);vfprintf(qtest,format,args);va_end(args);
    fputc('\n',qtest);fflush(qtest);char line[256];
    CHECK(fgets(line,sizeof(line),qtest) && !strncmp(line,"OK",2));
    return line[2]==' '?strtoul(line+3,NULL,0):0;
}
static void submitPacket(unsigned offset)
{
    if(useDoorbell){
        // A different ordinary register must remain selected even across
        // commands which block on the native renderer or reject input.
        command("outl 0xc000 2");
        command("outl 0x%x 0x%x",0xc000+VIM_GL_DOORBELL_PORT,offset);
        CHECK(command("inl 0xc000")==2);
    }else{
        command("outl 0xc000 0x%x",VIM_GL_REGISTER);
        command("outl 0xc001 0x%x",offset);
    }
}
static void exactRead(int fd,void *data,size_t bytes)
{
    unsigned char *p=data;
    while(bytes){ssize_t n=read(fd,p,bytes);if(n<0 && errno==EINTR)continue;CHECK(n>0);p+=n;bytes-=n;}
}
static void *exchangeServer(void *opaque)
{
    int listener=(int)(intptr_t)opaque,fd=accept(listener,NULL,NULL);CHECK(fd>=0);close(listener);
    const unsigned sends[]={8,144,4080,8},receives[]={16,144,4096,64};
    for(unsigned round=0;round<4;++round){
        unsigned char input[4096],output[4096];exactRead(fd,input,sends[round]);
        uint32_t sizes[2];memcpy(sizes,input,8);CHECK(sizes[0]==sends[round] && sizes[1]==receives[round]);
        for(unsigned i=8;i<sends[round];++i)CHECK(input[i]==(unsigned char)(i+round));
        for(unsigned i=0;i<receives[round];++i)output[i]=(unsigned char)(i*3+round);
        // Deliberately split replies. Last reply is truncated: no retry may
        // reuse the damaged stream/token or report partial success.
        unsigned count=round==3?16:receives[round];
        for(unsigned i=0;i<count;){unsigned n=count-i>7?7:count-i;CHECK(write(fd,output+i,n)==n);i+=n;}
    }
    close(fd);return NULL;
}
int main(int argc,char **argv)
{
    if(argc==4 && !strcmp(argv[1],"--unauthorized")){
        struct VIMVRAMMemoryMapping m={0};
        kern_return_t kr=VIMVRAMMemoryImport(argv[2],atoi(argv[3]),&m,1000);
        CHECK(kr==KERN_INVALID_ARGUMENT && !m.address && !m.bytes);
        puts("PASS different consumer PID rejected");return 0;
    }
    int directRead=argc==4 && !strcmp(argv[3],"--direct-read");
    testRAM=argc==4 && (!strcmp(argv[3],"--ram") || !strcmp(argv[3],"--doorbell-ram"));
    useDoorbell=argc==4 && (!strcmp(argv[3],"--doorbell") || !strcmp(argv[3],"--doorbell-ram"));
    if(argc!=3 && !directRead && !testRAM && !useDoorbell){fprintf(stderr,"Usage: %s /path/to/qemu-system-x86_64 /path/to/pc-bios [--direct-read|--ram|--doorbell|--doorbell-ram]\n",argv[0]);return 2;}
    atexit(cleanup);strcpy(scratch,"/tmp/vim-vram-device.XXXXXX");CHECK(mkdtemp(scratch));
    snprintf(socketPath,sizeof(socketPath),"%s/qtest",scratch);
    int listener=socket(AF_INET,SOCK_STREAM,0);CHECK(listener>=0);
    struct sockaddr_in loop={.sin_family=AF_INET,.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
    CHECK(!bind(listener,(void *)&loop,sizeof(loop)) && !listen(listener,1));
    socklen_t loopSize=sizeof(loop);CHECK(!getsockname(listener,(void *)&loop,&loopSize));
    pthread_t server;CHECK(!pthread_create(&server,NULL,exchangeServer,(void *)(intptr_t)listener));
    char service[128],pid[32],endpoint[200],port[80];
    snprintf(port,sizeof(port),"vmware-svga.passthrough-opengl-port=%u",ntohs(loop.sin_port));
    snprintf(service,sizeof(service),"com.virtualintelmac.test.qemu-vram.%d",getpid());
    snprintf(pid,sizeof(pid),"%d",getpid());
    setenv("VIM_OPENGL_VRAM_SERVICE",service,1);setenv("VIM_OPENGL_RENDERER_PID",pid,1);
    snprintf(endpoint,sizeof(endpoint),"unix:%s,server=on,wait=off",socketPath);
    char *args[]={argv[1],"-L",argv[2],"-machine","q35","-accel","tcg","-S","-m",testRAM?"4096":"128",
        "-nodefaults","-vga","vmware","-global","vmware-svga.passthrough-opengl-bridge=on",
        "-global",port,
        "-global","vmware-svga.vgamem_mb=128","-display","none","-monitor","none",
        "-serial","none","-qtest",endpoint,"-qtest-log","/dev/null",NULL};
    CHECK(!posix_spawn(&qemu,argv[1],NULL,NULL,args,environ));
    if(testRAM)strcat(service,".ram");
    int fd=socket(AF_UNIX,SOCK_STREAM,0);CHECK(fd>=0);
    struct sockaddr_un address;memset(&address,0,sizeof(address));address.sun_family=AF_UNIX;
    CHECK(strlen(socketPath)<sizeof(address.sun_path));strcpy(address.sun_path,socketPath);
    int connected=0;
    for(unsigned i=0;i<500;++i){
        if(!connect(fd,(void *)&address,sizeof(address))){connected=1;break;}
        CHECK(errno==ENOENT || errno==ECONNREFUSED);usleep(10000);
    }
    CHECK(connected);struct timeval timeout={10,0};
    setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));qtest=fdopen(fd,"r+");CHECK(qtest);
    mach_port_t ready=0;
    for(unsigned i=0;i<500;++i){
        if(!bootstrap_look_up(bootstrap_port,service,&ready))break;
        CHECK(kill(qemu,0)==0);usleep(10000);
    }
    CHECK(ready);mach_port_deallocate(mach_task_self(),ready);
    struct VIMVRAMMemoryMapping mapping={0},wrong={0},original={0},commandVRAM={0};
    CHECK(VIMVRAMMemoryImport(service,qemu+1,&wrong,1000)==KERN_INVALID_ARGUMENT && !wrong.address);
    char producer[32];snprintf(producer,sizeof(producer),"%d",qemu);
    char *unauthorized[]={argv[0],"--unauthorized",service,producer,NULL};pid_t other=0;
    CHECK(!posix_spawn(&other,argv[0],NULL,NULL,unauthorized,environ));int status=0;
    CHECK(waitpid(other,&status,0)==other && WIFEXITED(status) && !WEXITSTATUS(status));
    kern_return_t imported=VIMVRAMMemoryImport(service,qemu,&mapping,1000);
    printf("VRAM import status=%x bytes=%llu\n",imported,(unsigned long long)mapping.bytes);
    CHECK(!imported && mapping.bytes==(testRAM?4ull*1024*1024*1024:128u*1024u*1024u));
    if(testRAM){
        original=mapping;memset(&mapping,0,sizeof(mapping));
        CHECK(original.region_count>0);
        aliasPages=(65536u+2560u*1600u*4)/4096;
        uint64_t *pages=calloc(aliasPages,sizeof(*pages));CHECK(pages);
        for(unsigned i=0;i<aliasPages;++i)pages[i]=pageAddress(aliasPages-1-i);
        CHECK(!VIMSharedRAMMapPages(&original,pages,aliasPages,&mapping));free(pages);
        uint64_t hole=UINT64_MAX-4095;struct VIMVRAMMemoryMapping bad={0};
        CHECK(VIMSharedRAMMapPages(&original,&hole,1,&bad)==KERN_INVALID_ADDRESS && !bad.address);
        hole=0xa0000;CHECK(VIMSharedRAMMapPages(&original,&hole,1,&bad)==KERN_INVALID_ADDRESS && !bad.address);
        printf("main RAM regions=%u, reverse-permuted 4-KB aliases=%u; holes rejected\n",original.region_count,aliasPages);
    }
    unsigned device;
    for(device=0;device<32;++device){
        command("outl 0xcf8 0x%x",0x80000000u|(device<<11));
        if(command("inl 0xcfc")==0x040515ad)break;
    }
    CHECK(device<32);unsigned config=0x80000000u|(device<<11),bar=0xf0000000u,offset=65536;
    command("outl 0xcf8 0x%x",config|0x14);command("outl 0xcfc 0x%x",bar);
    command("outl 0xcf8 0x%x",config|0x10);command("outl 0xcfc 0xc001");
    command("outl 0xcf8 0x%x",config|4);command("outw 0xcfc 3");
    command("outl 0xc000 0x%x",VIM_GL_DOORBELL_CAPABILITIES_REGISTER);
    CHECK(command("inl 0xc001")==VIM_GL_DOORBELL_MAGIC);
    if(useDoorbell)submitPacket(UINT32_MAX); // reject truncated/out-of-range before mapping
    if(testRAM){
        char vramService[128];strcpy(vramService,service);vramService[strlen(vramService)-4]=0;
        CHECK(!VIMVRAMMemoryImport(vramService,qemu,&commandVRAM,1000));
    }
    const unsigned commandOffset=32u*1024u*1024u;
    struct VIMGLDevicePacket *dirty=(void *)(uintptr_t)((testRAM?commandVRAM.address:mapping.address)+commandOffset);
    command("writel 0x%llx 0xff0000ff",guestAddress(offset));
    uint32_t *pixels=(uint32_t *)(uintptr_t)(mapping.address+offset);CHECK(pixels[0]==0xff0000ff);
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj context=NULL;GLint count=0;
    CHECK(!CGLChoosePixelFormat(attrs,&pf,&count) && pf && !CGLCreateContext(pf,NULL,&context));
    CGLDestroyPixelFormat(pf);CHECK(!CGLSetCurrentContext(context));
    const char *renderer=(const char *)glGetString(GL_RENDERER);printf("QEMU VRAM renderer=%s\n",renderer);
    CHECK(renderer && strstr(renderer,"Intel"));
    const unsigned width=2560,height=1600;GLuint texture=0,fbo=0;
    /* Populate every backing page before asking GL to pin it. */
    for(size_t i=0;i<(size_t)width*height;++i)pixels[i]=0xff0000ff;
    CHECK(command("readl 0x%llx",guestAddress(offset+(width*height-1)*4))==0xff0000ff);
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    glTextureRangeAPPLE(GL_TEXTURE_RECTANGLE_ARB,width*height*4,pixels);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_STORAGE_HINT_APPLE,GL_STORAGE_SHARED_APPLE);
    glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE,GL_TRUE);
    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA8,width,height,0,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,pixels);
    glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE,GL_FALSE);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,texture,0);
    CHECK(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glDisable(GL_DITHER);
    /* Texture creation itself owns the pages until its initial GPU work has
     * completed; do not let a CPU BAR write race that setup transaction. */
    uint32_t initial=0;glReadPixels(0,0,1,1,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,&initial);
    glFinish();CHECK(!glGetError() && initial==0xff0000ff);
    GLuint sampleTexture=0,sampleFBO=0;
    glGenTextures(1,&sampleTexture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,sampleTexture);
    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA8,1,1,0,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,NULL);
    glGenFramebuffersEXT(1,&sampleFBO);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,sampleFBO);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,sampleTexture,0);
    CHECK(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    for(unsigned round=0;round<8;++round){
        unsigned sample=(width*height-1)*4,color=0xff000020u+round;
        command("writel 0x%llx 0x%x",guestAddress(offset+sample),color);
        uint32_t actual=0;
        if(directRead)glReadPixels(width-1,height-1,1,1,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,&actual);
        else {
            /* Test what GPU consumers see, not the driver's direct readback
             * shortcut for a client-storage FBO (which can return stale data). */
            glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,fbo);glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,sampleFBO);
            glBlitFramebufferEXT(width-1,height-1,width,height,0,0,1,1,GL_COLOR_BUFFER_BIT,GL_NEAREST);
            glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,sampleFBO);
            glReadPixels(0,0,1,1,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,&actual);
            glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
        }
        if(actual!=color){
            uint32_t opposite=0;glReadPixels(width-1,0,1,1,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,&opposite);
            fprintf(stderr,"round=%u CPU-alias=%08x GL=%08x opposite-row=%08x expected=%08x error=%x\n",round,pixels[width*height-1],actual,opposite,color,glGetError());
        }
        CHECK(actual==color);
        glClearColor((round&1)?1:0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);glFinish();CHECK(!glGetError());
        /* QEMU's independent device BAR reads see GPU results before any
         * render readback or CPU synchronization copy in this process. */
        uint32_t expected=(round&1)?0xffffff00u:0xff00ff00u;
        for(size_t i=0;i<(size_t)width*height;++i)CHECK(pixels[i]==expected);
        CHECK(command("readl 0x%llx",guestAddress(offset))==expected);
        CHECK(command("readl 0x%llx",guestAddress(offset+sample))==expected);
        CHECK(command("readl 0x%llx",guestAddress(offset+(937u*width+123u)*4))==expected);
        memset(dirty,0,sizeof(*dirty));dirty->magic=VIM_GL_MAGIC;dirty->action=12;dirty->result=-1;
        if(testRAM){
            dirty->token=1;dirty->bytes=aliasPages*4096;dirty->reserved=aliasPages;
            uint64_t *list=(void *)(dirty+1);for(unsigned i=0;i<aliasPages;++i)list[i]=pageAddress(aliasPages-1-i);
        } else {dirty->bytes=width*height*4;dirty->reserved=offset;}
        submitPacket(commandOffset);
        CHECK(dirty->result==(int32_t)dirty->bytes);
    }
    if(testRAM){
        ((uint64_t *)(dirty+1))[aliasPages-1]=0xa0000;dirty->result=-1;
        submitPacket(commandOffset);
        CHECK(dirty->result==-1);
        /* CPU publication marks every captured page, without moving pixels.
         * Bound the actual device-only cost for contiguous versus fragmented
         * metadata. This is not an animation or host/guest throughput claim.
         */
        uint64_t *list=(void *)(dirty+1);mach_timebase_info_data_t timebase;
        CHECK(!mach_timebase_info(&timebase));
        const unsigned counts[]={16,256,4096};
        const char *shapes[]={"contiguous","reverse","permuted"};
        for(unsigned shape=0;shape<3;++shape)for(unsigned size=0;size<3;++size){
            unsigned pages=counts[size];dirty->bytes=pages*4096;dirty->reserved=pages;
            for(unsigned i=0;i<pages;++i){
                unsigned index=shape==2?((i*2053u+17u)&(pages-1)):shape?pages-1-i:i;
                list[i]=0x400000ull+(uint64_t)index*4096;
            }
            uint64_t start=mach_absolute_time();
            for(unsigned pass=0;pass<32;++pass){
                dirty->result=-1;submitPacket(commandOffset);CHECK(dirty->result==(int32_t)dirty->bytes);
            }
            double us=(double)(mach_absolute_time()-start)*timebase.numer/timebase.denom/1000/32;
            printf("dirty-only %s pages=%u mean_submission_us=%.2f (includes qtest control)\n",
                shapes[shape],pages,us);
        }
        dirty->bytes=4096*4096;dirty->reserved=4096;
        for(unsigned i=0;i<4096;++i)list[i]=0x400000ull+(uint64_t)i*4096;
        const uint64_t rejected[]={0xa0000,0xf0000,UINT64_MAX-4095,0x400001};
        for(unsigned i=0;i<sizeof(rejected)/sizeof(rejected[0]);++i){
            list[4095]=rejected[i];dirty->result=-1;submitPacket(commandOffset);CHECK(dirty->result==-1);
        }
        // Duplicate pages are legal; publishing one address twice is still
        // not permission to include a hole or change a captured descriptor.
        dirty->bytes=8192;dirty->reserved=2;list[0]=list[1]=0x400000;
        dirty->result=-1;submitPacket(commandOffset);CHECK(dirty->result==8192);
        // Sorting a private DIRTY SET must never reorder actual pixel aliases
        // or modify the captured guest descriptor in place.
        CHECK(list[0]==0x400000&&list[1]==0x400000);
        for(size_t i=0;i<(size_t)width*height;++i)CHECK(pixels[i]==0xffffff00u);
        CHECK(command("readl 0x%llx",guestAddress(offset))==0xffffff00u);
    }
    command("outl 0xc000 0x%x",VIM_GL_EXCHANGE_CAPABILITIES_REGISTER);
    CHECK(command("inl 0xc001")==VIM_GL_EXCHANGE_MAGIC);
    memset(dirty,0,sizeof(*dirty));dirty->magic=VIM_GL_MAGIC;dirty->action=1;
    submitPacket(commandOffset);
    CHECK(dirty->result==0 && dirty->token);uint32_t streamToken=dirty->token;
    const unsigned sends[]={8,144,4080,8},receives[]={16,144,4096,64};
    for(unsigned round=0;round<4;++round){
        dirty->action=13;dirty->bytes=sends[round];dirty->reserved=receives[round];dirty->result=-1;
        unsigned char *payload=(void *)(dirty+1);memcpy(payload,&sends[round],4);memcpy(payload+4,&receives[round],4);
        for(unsigned i=8;i<sends[round];++i)payload[i]=(unsigned char)(i+round);
        if(!round){
            // Reject both bounds BEFORE writing anything to the server.
            dirty->reserved=4097;submitPacket(commandOffset);CHECK(dirty->result==-1);
            dirty->reserved=receives[round];dirty->bytes=4081;
            submitPacket(commandOffset);CHECK(dirty->result==-1);dirty->bytes=sends[round];
        }
        submitPacket(commandOffset);
        if(round==3)CHECK(dirty->result==-1);
        else {CHECK(dirty->result==(int32_t)receives[round]);for(unsigned i=0;i<receives[round];++i)CHECK(payload[i]==(unsigned char)(i*3+round));}
    }
    dirty->token=streamToken;dirty->result=0;
    submitPacket(commandOffset);CHECK(dirty->result==-1);
    CHECK(!pthread_join(server,NULL));puts("PASS small ordered exchange: boundaries, split replies, rejection before I/O, truncated stream invalidation");
    glDeleteFramebuffersEXT(1,&sampleFBO);glDeleteTextures(1,&sampleTexture);
    glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&texture);glFinish();
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);VIMVRAMMemoryUnmap(&mapping);VIMVRAMMemoryUnmap(&original);VIMVRAMMemoryUnmap(&commandVRAM);
    puts(testRAM?"PASS real QEMU anonymous main RAM: fragmented guest page order, CPU -> GPU -> CPU, peer authentication, 8 ownership cycles":
        "PASS real QEMU anonymous VMware VRAM: CPU BAR writes -> GL, GPU writes -> CPU BAR, exact peer authentication, 8 ownership cycles");
    return 0;
}
