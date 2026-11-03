/* Actual native Intel shared-target retirement, not a desktop FPS test.
 * The caller keeps exported backing alive until synchronous close returns.
 * No readback, IOSurface lock or diagnostic Finish may repair CPU pixels. */
#define VIM_PASSTHROUGH_OPENGL_INPROCESS 1
#define VIM_VRAM_RENDER_TARGET_EXPERIMENT 1
#define main DGExecutorMain
#include "../PassthroughOpenGL/passthrough-opengl-host.c"
#undef main
#include <assert.h>
enum {W=61,H=37,Pitch=64,Offset=65536,Bytes=12288};
static pthread_mutex_t ready_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ready_condition=PTHREAD_COND_INITIALIZER;
static int ready,explicit_destroy;
static void retirement(struct DGMemoryStream *stream)
{
    struct DGInterpreter *s=calloc(1,sizeof(*s));assert(s);s->next=2;s->hello=1;
    struct DGHostContext *c=&s->contexts[1];
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj format=NULL;GLint count=0;
    assert(!CGLChoosePixelFormat(attrs,&format,&count) && format);
    assert(!CGLCreateContext(format,NULL,&c->cgl));CGLDestroyPixelFormat(format);
    assert(!CGLSetCurrentContext(c->cgl));
    const char *renderer=(const char *)glGetString(GL_RENDERER);
    assert(renderer && strstr(renderer,"Intel"));printf("retirement renderer=%s\n",renderer);
    c->profile=kCGLOGLPVersion_Legacy;c->read_mode=c->draw_mode=GL_FRONT;
    assert(DGCreateAliases(c,NULL) && DGSelectDrawable(c,1,W,H));
    struct DGMessage begin={0};struct DGReply reply={0};
    begin.args[0]=Offset;begin.args[1]=Bytes;begin.args[2]=W;begin.args[3]=H;begin.args[4]=Pitch*4;
    begin.args[7]=W;begin.args[8]=H;begin.args[10]=1;begin.args[11]=1;begin.args[12]=1;
    assert(!DGVRAMRenderBegin(c,&begin,&reply) && reply.result==1 && !reply.error);
    glDisable(GL_SCISSOR_TEST);glColorMask(1,1,1,1);glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    // Destruction must finish even an abandoned, otherwise valid primitive.
    glBegin(GL_LINES);c->inside_begin=1;
    pthread_mutex_lock(&ready_lock);ready=1;pthread_cond_signal(&ready_condition);pthread_mutex_unlock(&ready_lock);
    unsigned char byte;assert(DGMemoryServerRead(stream,&byte,1)==0);
    if(explicit_destroy){
        struct DGMessage destroy={.op=DG_DESTROY,.context=1};
        assert(!DGInterpret(s,-1,&destroy));
        assert(!s->contexts[1].cgl);
    }
    DGInterpreterDestroy(s);
}
int main(void)
{
    assert(!mach_vm_map(mach_task_self(),&dg_vram.address,1024*1024,16383,VM_FLAGS_ANYWHERE,
        MACH_PORT_NULL,0,FALSE,VM_PROT_READ|VM_PROT_WRITE,VM_PROT_READ|VM_PROT_WRITE,VM_INHERIT_NONE));
    dg_vram.bytes=1024*1024;
    mach_port_t entry=0;memory_object_size_t size=dg_vram.bytes;
    assert(!mach_make_memory_entry_64(mach_task_self(),&size,dg_vram.address,
        VM_PROT_READ|VM_PROT_WRITE,&entry,MACH_PORT_NULL));
    for(unsigned mode=0;mode<3;++mode){
        explicit_destroy=mode==2;ready=0;
        volatile uint32_t *pixels=(void *)(uintptr_t)(dg_vram.address+Offset);
        memset((void *)pixels,0xa7,Bytes);
        struct DGMemoryStream *stream=DGMemoryStreamOpen(retirement);assert(stream);
        pthread_mutex_lock(&ready_lock);while(!ready)pthread_cond_wait(&ready_condition,&ready_lock);pthread_mutex_unlock(&ready_lock);
        if(mode==1)assert(!DGMemoryStreamShutdown(stream));
        assert(!DGMemoryStreamCloseAndWait(stream) && !DGMemoryStreamLiveCount());
        // FIRST observation after close; no GL calls on this CPU thread.
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<Pitch;++x)
            assert(pixels[y*Pitch+x]==(x<W?0xffff0000u:0xa7a7a7a7u));
        for(unsigned i=Pitch*H*4;i<Bytes;++i)assert(((volatile unsigned char *)pixels)[i]==0xa7);
        printf("PASS %s: native GPU writes complete before backing reuse; padding intact\n",
            mode==0?"normal EOF":mode==1?"cancelled stream":"explicit context destroy");
        // Reuse belongs to the CPU only after the verified retirement.
        memset((void *)pixels,0x35,Bytes);
    }
    mach_port_deallocate(mach_task_self(),entry);VIMVRAMMemoryUnmap(&dg_vram);return 0;
}
