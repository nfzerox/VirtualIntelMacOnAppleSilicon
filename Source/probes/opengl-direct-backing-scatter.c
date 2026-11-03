/* Actual native Intel GPU -> anonymous exported RAM SG pages. FIRST CPU
 * contents/padding are checked after the actual producer fence and scatter;
 * no repairing GL readback, page alias rebuild or guest file replacement. */
#define VIM_PASSTHROUGH_OPENGL_INPROCESS 1
#define main DGExecutorMain
#include "../PassthroughOpenGL/passthrough-opengl-host.c"
#undef main
#include "../PassthroughOpenGL/direct-backing-scatter-host.h"
#include <assert.h>
#ifdef VIM_TEST_SCATTER_SMALL
enum {W=61,H=37,Pitch=64,Pages=3,RAMPages=16};
#else
enum {W=2468,H=82,Pitch=2472,Pages=(Pitch*H*4+4095)/4096,RAMPages=256};
#endif
static uint32_t pattern(unsigned x,unsigned y,unsigned n)
{return 0xff000000u|((x*13+y*3+n*7)&255)|(((x*7+y*17+n*11)&255)<<8)|(((x+y*11+n*19)&255)<<16);}
static void draw(unsigned round)
{
    glDisable(GL_DITHER);glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);
    glDisable(GL_FRAMEBUFFER_SRGB_EXT);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glViewport(0,0,W,H);glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,W,0,H,-1,1);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    // Different pixels across rows and page boundaries expose orientation,
    // channel, pitch and SG-order mistakes that a solid clear would miss.
    glBegin(GL_POINTS);
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
        uint32_t p=pattern(x,y,round);glColor4ub((p>>16)&255,(p>>8)&255,p&255,p>>24);
        glVertex2f(x+.5f,y+.5f);
    }
    glEnd();
}
static void exact(const struct VIMVRAMMemoryMapping *ram,const uint64_t *pages,unsigned n)
{
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<Pitch;++x){
        size_t offset=((size_t)y*Pitch+x)*4;
        uint64_t physical=pages[offset/4096]-0x10000000;
        uint32_t actual=*(volatile uint32_t *)(uintptr_t)(ram->address+physical+(offset&4095));
        assert(actual==(x<W?pattern(x,y,n):0xa7a7a7a7u));
    }
    for(size_t offset=(size_t)Pitch*H*4;offset<(size_t)Pages*4096;++offset){
        uint64_t physical=pages[offset/4096]-0x10000000;
        assert(*(volatile unsigned char *)(uintptr_t)(ram->address+physical+(offset&4095))==0xa7);
    }
}
static void sample_source(GLuint source)
{
    glDisable(GL_DITHER);glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);
    glDisable(GL_FRAMEBUFFER_SRGB_EXT);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glViewport(0,0,W,H);glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,W,0,H,-1,1);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glEnable(GL_TEXTURE_RECTANGLE_ARB);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,source);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    glBegin(GL_QUADS);
    glTexCoord2i(0,0);glVertex2i(0,0);glTexCoord2i(W,0);glVertex2i(W,0);
    glTexCoord2i(W,H);glVertex2i(W,H);glTexCoord2i(0,H);glVertex2i(0,H);
    glEnd();glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
}
static void wait_gpu(void)
{
    GLsync fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);assert(fence);glFlush();
    GLenum done;do{done=glClientWaitSync(fence,0,20000000);}while(done==GL_TIMEOUT_EXPIRED);
    assert(done==GL_ALREADY_SIGNALED || done==GL_CONDITION_SATISFIED);glDeleteSync(fence);
}
int main(int argc,char **argv)
{
    int alias_ordering=argc>=2 && !strcmp(argv[1],"--alias-ordering");
    unsigned alias_rounds=1;
    if(alias_ordering && argc==3){
        char *end=NULL;unsigned long n=strtoul(argv[2],&end,10);
        if(!end || *end || !n || n>1000)return 2;alias_rounds=(unsigned)n;
    }
    if(argc!=1 && (!alias_ordering || argc>3)){
        fprintf(stderr,"Usage: %s [--alias-ordering [1..1000]]\n",argv[0]);return 2;
    }
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj pf=NULL;GLint count=0;struct DGHostContext c={0};
    assert(!CGLChoosePixelFormat(attrs,&pf,&count) && pf);
    assert(!CGLCreateContext(pf,NULL,&c.cgl) && !CGLSetCurrentContext(c.cgl));CGLDestroyPixelFormat(pf);
    printf("scatter native renderer=%s\n",glGetString(GL_RENDERER));
    assert(strstr((const char *)glGetString(GL_RENDERER),"Intel"));
    c.profile=kCGLOGLPVersion_Legacy;c.draw_mode=c.read_mode=GL_FRONT;
    assert(DGCreateAliases(&c,NULL) && DGSelectDrawable(&c,1,W,H));
    struct VIMVRAMMemoryMapping original={0},ram={0};original.bytes=RAMPages*4096;
    assert(!mach_vm_allocate(mach_task_self(),&original.address,original.bytes,VM_FLAGS_ANYWHERE));
    mach_port_t memory=MACH_PORT_NULL;memory_object_size_t size=original.bytes;
    assert(!mach_make_memory_entry_64(mach_task_self(),&size,original.address,
        VM_PROT_READ|VM_PROT_WRITE,&memory,MACH_PORT_NULL));
    assert(!mach_vm_map(mach_task_self(),&ram.address,original.bytes,0,VM_FLAGS_ANYWHERE,
        memory,0,FALSE,VM_PROT_READ|VM_PROT_WRITE,VM_PROT_READ|VM_PROT_WRITE,VM_INHERIT_NONE));
    ram.bytes=original.bytes;ram.region_count=1;
    ram.regions[0]=(struct VIMSharedRAMRange){0x10000000,0,original.bytes};
    uint64_t pages[Pages];for(unsigned i=0;i<Pages;++i)pages[i]=0x10000000+(uint64_t)((i*109)%RAMPages)*4096;
    struct DGScatterPlan *plan=DGScatterPlanCreate(&ram,pages,Pages,W,H,Pitch*4);assert(plan);
    uint64_t saved=pages[1];pages[1]=pages[0];assert(!DGScatterPlanCreate(&ram,pages,Pages,W,H,Pitch*4));
    pages[1]=UINT64_MAX-4095;assert(!DGScatterPlanCreate(&ram,pages,Pages,W,H,Pitch*4));pages[1]=saved;
    assert(!DGScatterPlanCreate(&ram,pages,Pages,W,H,Pitch*4-1));
    DGScatterPlanRetain(plan);DGScatterPlanPut(plan);
    for(unsigned round=0;round<4;++round){
        memset((void *)(uintptr_t)original.address,0xa7,original.bytes);draw(round);
        GLsync fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);assert(fence);glFlush();
        GLenum done;do{done=glClientWaitSync(fence,0,20000000);}while(done==GL_TIMEOUT_EXPIRED);
        assert(done==GL_ALREADY_SIGNALED || done==GL_CONDITION_SATISFIED);
        assert(DGScatterCopyCompletedSurface(plan,c.color_storage));
        exact(&ram,pages,round);exact(&original,pages,round);glDeleteSync(fence);
        assert(!glGetError());
    }
    puts("PASS native GPU patterned pixels -> exported fragmented RAM: FIRST CPU pixels, both aliases, pitch/page padding, invalid/duplicate page refusal, no repairing readback");
    struct DGHostContext consumer={0};
    assert(!CGLCreateContext(CGLGetPixelFormat(c.cgl),c.cgl,&consumer.cgl));
    consumer.profile=kCGLOGLPVersion_Legacy;consumer.draw_mode=consumer.read_mode=GL_FRONT;
    CGLSetCurrentContext(consumer.cgl);
    assert(DGCreateAliases(&consumer,&c) && DGSelectDrawable(&consumer,1,W,H));
    for(unsigned round=20;round<24;++round){
        // Queue a real sharing-context GPU read of the OLD source first.
        // The writable IOSurface lock must preserve that native dependency.
        CGLSetCurrentContext(consumer.cgl);sample_source(c.color);glFlush();
        memset((void *)(uintptr_t)original.address,0xa7,original.bytes);
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
            size_t offset=((size_t)y*Pitch+x)*4;
            *(uint32_t *)(uintptr_t)(ram.address+pages[offset/4096]-0x10000000+(offset&4095))=pattern(x,y,round);
        }
        CGLSetCurrentContext(c.cgl);glFlush();
        assert(DGScatterGatherSurface(plan,c.color_storage));
        // Native GPU sampling must use the initialized IOSurface, not keep
        // reading guest RAM or require a repairing TexImage/ReadPixels call.
        memset((void *)(uintptr_t)original.address,0xa7,original.bytes);
        CGLSetCurrentContext(consumer.cgl);sample_source(c.color);wait_gpu();
        assert(DGScatterCopyCompletedSurface(plan,consumer.color_storage));
        exact(&ram,pages,round);exact(&original,pages,round);assert(!glGetError());
    }
    puts("PASS CPU-current fragmented RAM -> IOSurface -> sharing-context GPU sampling -> FIRST CPU bytes: old GPU readers, channels/rows/padding, no reimport/readback repair");
    if(alias_ordering){
        CGLSetCurrentContext(c.cgl);
        uint32_t lease=DGSurfaceAcquire(&c,46086,1,W,H);assert(lease);
        assert(!DGSurfaceDrawable(&c,DGSurfaceFind(&c,lease)));
        uint32_t *blue=malloc(W*H*4);assert(blue);for(unsigned i=0;i<W*H;++i)blue[i]=0xff0000ffu;
        for(unsigned variant=0;variant<10;++variant){
#ifndef VIM_VRAM_RENDER_TARGET_EXPERIMENT
          if(variant==9)continue;
#endif
          unsigned failed=0;
          for(unsigned round=0;round<alias_rounds;++round){
            CGLSetCurrentContext(c.cgl);
            memset((void *)(uintptr_t)original.address,0xa7,original.bytes);
            assert(DGScatterGatherSurface(plan,c.color_storage));
            uint32_t initial[W*H];glPixelStorei(GL_PACK_ROW_LENGTH,0);glPixelStorei(GL_PACK_ALIGNMENT,4);
            glReadPixels(0,0,W,H,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,initial);
            for(unsigned i=0;i<W*H;++i)assert(initial[i]==0xa7a7a7a7u);
#ifdef VIM_TEST_ALIAS_PRESSURE
            // Real queued GPU work widens producer/consumer overlap. This
            // diagnoses native ordering; it is not a sleep or timed fence.
            for(unsigned queued=0;queued<VIM_TEST_ALIAS_PRESSURE;++queued)draw(queued);
#endif
            glDisable(GL_SCISSOR_TEST);glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glFlush();
            GLuint alias=0;glGenTextures(1,&alias);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,alias);
            assert(!CGLTexImageIOSurface2D(c.cgl,GL_TEXTURE_RECTANGLE_ARB,GL_RGBA8,W,H,
                GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,c.color_storage,0));
            assert(DGRegisterAlias(&c,alias,c.color_storage));
            GLsync producer=NULL;
            if(variant==5 || variant==6){producer=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);assert(producer);glFlush();}
            if(variant==7 || variant==8)wait_gpu();
            CGLSetCurrentContext(consumer.cgl);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,alias);
            if(producer)glWaitSync(producer,0,GL_TIMEOUT_IGNORED);
#ifdef VIM_VRAM_RENDER_TARGET_EXPERIMENT
            if(variant==9){
                uint64_t waits=dg_scoped_alias_cpu_waits;
                assert(DGPrepareScopedTextureCPUWrite(&consumer,GL_TEXTURE_RECTANGLE_ARB));
                assert(dg_scoped_alias_cpu_waits==waits); // no active borrowed writer
                c.vram_render_serial=round+1;
                glBindTexture(GL_TEXTURE_RECTANGLE_ARB,consumer.color);
                assert(DGPrepareScopedTextureCPUWrite(&consumer,GL_TEXTURE_RECTANGLE_ARB));
                assert(dg_scoped_alias_cpu_waits==waits); // unrelated storage
                glBindTexture(GL_TEXTURE_RECTANGLE_ARB,alias);
                glEnable(0xffffffffu); // preserve the consumer's existing native error
                assert(DGPrepareScopedTextureCPUWrite(&consumer,GL_TEXTURE_RECTANGLE_ARB));
                assert(dg_scoped_alias_cpu_waits==waits+1 && CGLGetCurrentContext()==consumer.cgl);
                assert(glGetError()==GL_INVALID_ENUM && !glGetError());
                c.vram_render_serial=0;
            }
#endif
            glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE,0);glPixelStorei(GL_UNPACK_ALIGNMENT,4);
            glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
            glTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB,0,0,0,W,H,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,blue);
            GLsync other=NULL;
            if(variant==1 || variant>=3)other=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
            glFlush();if(variant==2)glFinish();
            CGLSetCurrentContext(c.cgl);if(other)glWaitSync(other,0,GL_TIMEOUT_IGNORED);
            wait_gpu();IOSurfaceRef read=c.color_storage;
            if(variant>=3 && variant!=5 && variant!=7 && variant!=9){
                CGLSetCurrentContext(consumer.cgl);sample_source(variant==3 || variant==6 || variant==8?c.color:alias);wait_gpu();
                read=consumer.color_storage;
            }
            assert(DGScatterCopyCompletedSurface(plan,read));unsigned reds=0,blues=0,others=0;
            for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
                size_t offset=((size_t)y*Pitch+x)*4;
                uint32_t p=*(volatile uint32_t *)(uintptr_t)(ram.address+pages[offset/4096]-0x10000000+(offset&4095));
                if(p==0xffff0000u)++reds;else if(p==0xff0000ffu)++blues;else ++others;
            }
            if(reds || others || alias_rounds==1)
                printf("ALIAS_ORDER variant=%u round=%u red=%u blue=%u other=%u renderer=%s\n",
                    variant,round,reds,blues,others,glGetString(GL_RENDERER));
            failed+=reds || others;
#ifdef VIM_VRAM_RENDER_TARGET_EXPERIMENT
            if(variant==9)assert(!reds && !others && blues==W*H);
#endif
            DGForgetAliases(&consumer,1,&alias);
            assert(!glGetError());if(other)glDeleteSync(other);if(producer)glDeleteSync(producer);glDeleteTextures(1,&alias);
          }
          printf("ALIAS_ORDER_SUMMARY variant=%u size=%ux%u rounds=%u failed=%u\n",variant,W,H,alias_rounds,failed);
        }
        free(blue);
    }
    DGDestroyDrawables(&consumer);DGDestroyAliases(&consumer);CGLSetCurrentContext(NULL);CGLDestroyContext(consumer.cgl);
    CGLSetCurrentContext(c.cgl);
    DGScatterPlanPut(plan);DGDestroySurfaceLeases(&c);DGDestroyDrawables(&c);DGDestroyAliases(&c);
    CGLSetCurrentContext(NULL);CGLDestroyContext(c.cgl);
    VIMVRAMMemoryUnmap(&ram);VIMVRAMMemoryUnmap(&original);mach_port_deallocate(mach_task_self(),memory);
    return 0;
}
