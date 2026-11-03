/* Actual executor helpers, native Intel. No system/guest binary replacement.
 * CPU backing stays excluded until completed End; source is either shared
 * VRAM or independently aliased, reverse-ordered 4-KiB RAM pages. */
#define VIM_VRAM_RENDER_TARGET_EXPERIMENT 1
#define main DGExecutorMain
#include "../PassthroughOpenGL/passthrough-opengl-host.c"
#undef main
#include <assert.h>
enum {W=2468,H=82,Pitch=2472,Offset=65536,Pages=(Pitch*H*4+4095)/4096};
static int finish_object;
static int ordered_pages;
static int exported_ram=1;
static int benchmark_only;
static int failures;
static uint32_t pattern(unsigned x,unsigned y,unsigned round)
{return 0xff000000u|((x*13+y*3+round*7)&255)|(((x*7+y*17+round*11)&255)<<8)|(((x+y*11+round*19)&255)<<16);}
static int blended(unsigned x,unsigned y)
{return x>=17 && x<W-13 && y>=9 && y<H-9 && (x<Pitch/2-10 || x>=Pitch/2+10);}
static int blendMatches(uint32_t actual,uint32_t base)
{
    const unsigned source[]={192,128,64};if((actual>>24)!=(base>>24))return 0;
    for(unsigned c=0;c<3;++c){
        int expected=(int)((source[c]*128+((base>>(c*8))&255)*127+127)/255);
        if(abs((int)((actual>>(c*8))&255)-expected)>1)return 0;
    }
    return 1;
}
static void draw(unsigned round)
{
    glDisable(GL_SCISSOR_TEST);glDisable(GL_FRAMEBUFFER_SRGB_EXT);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    if(!(round&1)){glClearColor(.2f,.4f,.6f,1);glClear(GL_COLOR_BUFFER_BIT);}
    glViewport(0,0,W,H);glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,W,0,H,-1,1);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_FALSE);glColor4ub(64,128,192,128);glEnable(GL_SCISSOR_TEST);
    for(unsigned half=0;half<2;++half){
        int left=half?Pitch/2+10:17,right=half?W-13:Pitch/2-10;
        glScissor(left,9,right-left,H-18);glBegin(GL_QUADS);
        glVertex2i(0,0);glVertex2i(W,0);glVertex2i(W,H);glVertex2i(0,H);glEnd();
    }
}
static int compareDouble(const void *a,const void *b)
{double x=*(const double *)a,y=*(const double *)b;return (x>y)-(x<y);}
static void benchmark(void)
{
    struct DGHostContext contexts[2]={{0}};
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj pf=NULL;GLint count=0;assert(!CGLChoosePixelFormat(attrs,&pf,&count) && pf);
    for(unsigned mode=0;mode<2;++mode){
        struct DGHostContext *c=contexts+mode;assert(!CGLCreateContext(pf,NULL,&c->cgl));CGLSetCurrentContext(c->cgl);
        assert(strstr((const char *)glGetString(GL_RENDERER),"Intel"));
        c->profile=kCGLOGLPVersion_Legacy;c->draw_mode=c->read_mode=GL_FRONT;
        assert(DGCreateAliases(c,NULL) && DGSelectDrawable(c,1,W,H));glDisable(GL_DITHER);
    }
    CGLDestroyPixelFormat(pf);
    struct {struct DGMessage m;union {uint64_t pages[Pages];struct DGVRAMRect rect;};} p={0};
    uint64_t pages[Pages];for(unsigned i=0;i<Pages;++i)pages[i]=0x10000000u+(Pages-i-1)*4096;
    struct VIMVRAMMemoryMapping alias={0};assert(!VIMSharedRAMMapPages(&dg_ram,pages,Pages,&alias));
    uint32_t *pixels=(void *)(uintptr_t)alias.address;
    uint64_t *a=p.m.args;a[1]=Pages*4096;a[2]=W;a[3]=H;a[4]=Pitch*4;
    a[7]=W;a[8]=H;a[10]=10000;a[11]=1;a[13]=1;
    double times[2][32],sum[2]={0};unsigned samples[2]={0};
    mach_timebase_info_data_t clock;mach_timebase_info(&clock);
    // Eight warmups, then interleaved ABBA samples. CPU fill / validation /
    // context selection are outside the measured ownership transaction.
    for(unsigned n=0;n<72;++n){
        unsigned mode=(n%4==1 || n%4==2),round=n;
        struct DGHostContext *c=contexts+mode;CGLSetCurrentContext(c->cgl);
        memset(pixels,0xa7,Pages*4096);
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)pixels[y*Pitch+x]=pattern(x,y,round);
        a[12]=n+1;a[14]=Pages;p.m.bytes=sizeof(pages);memcpy(p.pages,pages,sizeof(pages));
        struct DGReply reply={0};uint64_t started=mach_absolute_time();
        if(mode)assert(!DGVRAMRenderBegin(c,&p.m,&reply) && reply.result==1 && !reply.error);
        else{p.m.op=DG_VRAM_IMPORT_DRAWABLE;assert(!DGVRAMTransfer(c,&p.m,&reply) && reply.result==1 && !reply.error);}
        draw(round);
        if(mode)assert(DGVRAMRenderEnd(c));
        else{
            p.m.op=DG_VRAM_PUBLISH_DRAWABLE;a[14]=0;p.m.bytes=sizeof(p.rect);
            p.rect=(struct DGVRAMRect){0,0,W,H};memset(&reply,0,sizeof(reply));
            assert(!DGVRAMTransfer(c,&p.m,&reply) && reply.result==1 && !reply.error);
        }
        double ms=(mach_absolute_time()-started)*(double)clock.numer/clock.denom/1e6;
        if(n>=8){assert(samples[mode]<32);times[mode][samples[mode]++]=ms;sum[mode]+=ms;}
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<Pitch;++x){
            uint32_t actual=((volatile uint32_t *)pixels)[y*Pitch+x],base=(round&1)?pattern(x,y,round):0xff336699u;
            assert(x>=W?actual==0xa7a7a7a7u:blended(x,y)?blendMatches(actual,base):actual==base);
        }
        assert(!glGetError());
    }
    for(unsigned mode=0;mode<2;++mode){
        qsort(times[mode],32,sizeof(double),compareDouble);
        printf("BENCH %s samples=32 mean_ms=%.6f median_ms=%.6f p95_ms=%.6f max_ms=%.6f; ALL CPU/padding pixels verified; not desktop FPS\n",
            mode?"direct-render/private-snapshot/completion":"import/render/publish/completion",
            sum[mode]/32,times[mode][15],times[mode][30],times[mode][31]);
        CGLSetCurrentContext(contexts[mode].cgl);DGDestroyDrawables(contexts+mode);DGDestroyAliases(contexts+mode);
        CGLSetCurrentContext(NULL);CGLDestroyContext(contexts[mode].cgl);
    }
    VIMVRAMMemoryUnmap(&alias);
}
static void exercise(int ram)
{
    int passed=1;
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj pf=NULL;GLint count=0;struct DGHostContext c={0};
    assert(!CGLChoosePixelFormat(attrs,&pf,&count) && pf && !CGLCreateContext(pf,NULL,&c.cgl));
    CGLDestroyPixelFormat(pf);assert(!CGLSetCurrentContext(c.cgl));
    const char *renderer=(const char *)glGetString(GL_RENDERER),*vendor=(const char *)glGetString(GL_VENDOR);
    printf("Native render transaction RAM=%d vendor=%s renderer=%s\n",ram,vendor,renderer);fflush(stdout);
    assert(strstr(renderer,"Intel") && strstr(vendor,"Intel"));
    c.profile=kCGLOGLPVersion_Legacy;c.read_mode=c.draw_mode=GL_FRONT;
    assert(DGCreateAliases(&c,NULL) && DGSelectDrawable(&c,1,W,H));
    struct {struct DGMessage m;uint64_t pages[Pages];} p={0};uint64_t *a=p.m.args;
    a[0]=ram?0:Offset;a[1]=Pages*4096;a[2]=W;a[3]=H;a[4]=Pitch*4;
    a[7]=W;a[8]=H;a[10]=1000+ram;a[11]=1;a[12]=1;a[13]=ram;
    struct VIMVRAMMemoryMapping alias={0};
    if(ram){
        for(unsigned i=0;i<Pages;++i)p.pages[i]=0x10000000u+(ordered_pages?i:Pages-i-1)*4096;
        assert(!VIMSharedRAMMapPages(&dg_ram,p.pages,Pages,&alias));
        a[14]=Pages;p.m.bytes=sizeof(p.pages);
    }
    uint32_t *pixels=(void *)(uintptr_t)(ram?alias.address:dg_vram.address+Offset);
    GLuint private_color=c.color;IOSurfaceRef private_storage=c.color_storage;
    struct DGReply reply={0};struct DGMessage bad=p.m;
    bad.args[5]=1;assert(!DGVRAMRenderBegin(&c,&bad,&reply) && !reply.result && c.color==private_color);
    bad=p.m;bad.args[9]=1;assert(!DGVRAMRenderBegin(&c,&bad,&reply) && !reply.result);
    c.double_buffer=1;assert(!DGVRAMRenderBegin(&c,&p.m,&reply) && !reply.result);c.double_buffer=0;
    GLuint exported=0;glGenTextures(1,&exported);assert(DGRegisterAlias(&c,exported,private_storage));
    assert(!DGVRAMRenderBegin(&c,&p.m,&reply) && !reply.result && !c.vram_render_drawable);
    DGForgetAliases(&c,1,&exported);glDeleteTextures(1,&exported);
    GLuint sentinel=0;glGenTextures(1,&sentinel);glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,sentinel);
    glPixelStorei(GL_UNPACK_ALIGNMENT,8);glPixelStorei(GL_UNPACK_ROW_LENGTH,13);
    glPixelStorei(GL_UNPACK_SKIP_ROWS,1);glPixelStorei(GL_UNPACK_SKIP_PIXELS,2);glPixelStorei(GL_UNPACK_SWAP_BYTES,GL_TRUE);
    glDisable(GL_DITHER);
    for(unsigned round=0;round<8;++round){
        memset(pixels,0xa7,Pages*4096);
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)pixels[y*Pitch+x]=pattern(x,y,round);
        a[12]=round+1;
        // A real earlier native error must survive internal setup.
        glEnable(0xbad);memset(&reply,0,sizeof(reply));
        assert(!DGVRAMRenderBegin(&c,&p.m,&reply) && reply.result==1 && !reply.error);
        assert(c.error==GL_INVALID_ENUM);c.error=0;
        assert(c.vram_render_drawable==c.drawable && c.color!=private_color && !c.color_storage);
        if(ram && !round){
            const volatile uint32_t *source=(const void *)(uintptr_t)(dg_ram.address+(p.pages[0]-0x10000000u));
            const volatile uint32_t *mapped=(const void *)(uintptr_t)c.vram->alias.address;
            printf("RAM alias control: original=%08x CPU-alias=%08x GPU-map-CPU=%08x\n",source[0],((volatile uint32_t *)pixels)[0],mapped[0]);fflush(stdout);
            assert(source[0]==pattern(0,0,round) && mapped[0]==pattern(0,0,round));
        }
        GLint v=0;glGetIntegerv(GL_ACTIVE_TEXTURE,&v);assert(v==GL_TEXTURE1);
        glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&v);assert((GLuint)v==sentinel);
        glGetIntegerv(GL_UNPACK_ALIGNMENT,&v);assert(v==8);
        glGetIntegerv(GL_UNPACK_ROW_LENGTH,&v);assert(v==13);
        glGetIntegerv(GL_UNPACK_SKIP_ROWS,&v);assert(v==1);
        glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&v);assert(v==2);
        glGetIntegerv(GL_UNPACK_SWAP_BYTES,&v);assert(v==GL_TRUE);
        assert(!glGetError());
        // Active registration cannot be replaced or recursively borrowed.
        memset(&reply,0,sizeof(reply));assert(!DGVRAMRenderBegin(&c,&p.m,&reply) && !reply.result);
        assert(!DGVRAMBacking(&c,&p.m,1));
        draw(round);
        if(round&1){
            // Read/export fallback snapshots once, but subsequent private
            // drawing must still be published at transaction completion.
            assert(DGVRAMRenderMaterialize(&c));assert(c.vram_render_materialized && c.color==private_color);
            assert(DGVRAMRenderMaterialize(&c));
            glDisable(GL_FRAMEBUFFER_SRGB_EXT);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
            glScissor(2,2,6,5);glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
        }
        if(finish_object)glFinishObjectAPPLE(GL_TEXTURE,c.vram->texture);
        assert(DGVRAMRenderEnd(&c));
        // Inspect CPU bytes before any extra GL readback/query can help.
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<Pitch;++x){
            uint32_t actual=((volatile uint32_t *)pixels)[y*Pitch+x],base=(round&1)?pattern(x,y,round):0xff336699u;
            int blue=(round&1) && x>=2 && x<8 && y>=2 && y<7;
            int good=x>=W?actual==0xa7a7a7a7u:blue?actual==0xff0000ffu:blended(x,y)?blendMatches(actual,base):actual==base;
            if(!good){
                // Preserve the FIRST failed CPU observation. Only afterward
                // inspect the private materialization, never turn a diagnostic
                // readback or a late second CPU read into a passing verdict.
                glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,c.framebuffer);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
                glPixelStorei(GL_PACK_ALIGNMENT,4);glPixelStorei(GL_PACK_ROW_LENGTH,0);
                uint32_t gpu=0;glReadPixels(x,y,1,1,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,&gpu);
                uint32_t after_read=((volatile uint32_t *)pixels)[y*Pitch+x];
                glFinishObjectAPPLE(GL_TEXTURE,c.vram->texture);
                fprintf(stderr,"FAIL RAM=%d round=%u xy=%u,%u first_CPU=%08x expected_base=%08x materialized_GPU=%08x second_CPU=%08x error=%x\n",
                    ram,round,x,y,actual,base,gpu,after_read,glGetError());
                fprintf(stderr,"POST-FAILURE diagnostic resource finish CPU=%08x (original failed verdict retained)\n",((volatile uint32_t *)pixels)[y*Pitch+x]);
                passed=0;++failures;goto cleanup;
            }
            assert(good);
        }
        for(unsigned i=Pitch*H*4;i<Pages*4096;++i)assert(((unsigned char *)pixels)[i]==0xa7);
        assert(!c.vram_render_drawable && c.color==private_color && c.color_storage==private_storage && !glGetError());
        // A no-op End must not perform a second publish or invent an error.
        assert(DGVRAMRenderEnd(&c));
    }
    bad=p.m;bad.args[12]--;memset(&reply,0,sizeof(reply));
    assert(!DGVRAMRenderBegin(&c,&bad,&reply) && !reply.result && !c.vram_render_drawable);
    // An earlier ordinary pitched registration remains a snapshot/copy
    // resource. Do not silently reinterpret it as an exact-width target.
    DGVRAMDelete(&c);p.m.op=DG_VRAM_IMPORT_DRAWABLE;a[12]++;
    memset(&reply,0,sizeof(reply));assert(!DGVRAMTransfer(&c,&p.m,&reply) && reply.result==1);
    assert(c.vram->native_width==Pitch);
    memset(&reply,0,sizeof(reply));assert(!DGVRAMRenderBegin(&c,&p.m,&reply) && !reply.result && !c.vram_render_drawable);
    DGVRAMDelete(&c);
    // An active transaction must also drain/restore before drawable teardown.
    a[12]++;memset(&reply,0,sizeof(reply));assert(!DGVRAMRenderBegin(&c,&p.m,&reply) && reply.result==1);
    glDisable(GL_SCISSOR_TEST);glColorMask(1,1,1,1);glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    if(finish_object)glFinishObjectAPPLE(GL_TEXTURE,c.vram->texture);
    DGDropDrawable(&c,1);assert(!c.vram_render_drawable && !c.drawable);
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)assert(pixels[y*Pitch+x]==0xffff0000u);
    // Context destruction, unlike a public Drop, must also safely retire an
    // abandoned primitive before the alias/page registration can disappear.
    assert(DGSelectDrawable(&c,2,W,H));a[12]++;
    memset(&reply,0,sizeof(reply));assert(!DGVRAMRenderBegin(&c,&p.m,&reply) && reply.result==1);
    glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_LINES);c.inside_begin=1;assert(!DGVRAMRenderEnd(&c));
    DGDropDrawable(&c,2);assert(c.vram_render_drawable && c.error==GL_INVALID_OPERATION);
    DGDestroyDrawables(&c);assert(!c.vram_render_drawable && !c.inside_begin);
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)assert(((volatile uint32_t *)pixels)[y*Pitch+x]==0xff0000ffu);
cleanup:
    DGDestroyDrawables(&c);DGDestroyAliases(&c);glDeleteTextures(1,&sentinel);
    if(ram)VIMVRAMMemoryUnmap(&alias);CGLSetCurrentContext(NULL);CGLDestroyContext(c.cgl);
    if(passed)printf("PASS RAM=%d exact-width direct target: CPU-newer pixels, clear/blend/clip/padding, native setup state/error, private fallback plus later draws, old epoch/export/double-buffer/crop rejection and active teardown\n",ram);
}
int main(int argc,char **argv)
{
    if(argc==2 && !strcmp(argv[1],"--finish-object"))finish_object=1;
    else if(argc==2 && !strcmp(argv[1],"--ordered-pages"))ordered_pages=1;
    else if(argc==2 && !strcmp(argv[1],"--exported-ram"))exported_ram=1;
    else if(argc==2 && !strcmp(argv[1],"--private-ram"))exported_ram=0;
    else if(argc==2 && !strcmp(argv[1],"--benchmark"))benchmark_only=1;
    else if(argc!=1){fprintf(stderr,"Usage: %s [--finish-object|--ordered-pages|--exported-ram|--private-ram|--benchmark]\n",argv[0]);return 2;}
    assert(!mach_vm_map(mach_task_self(),&dg_vram.address,20*1024*1024,16383,VM_FLAGS_ANYWHERE,
        MACH_PORT_NULL,0,FALSE,VM_PROT_READ|VM_PROT_WRITE,VM_PROT_READ|VM_PROT_WRITE,VM_INHERIT_NONE));
    dg_vram.bytes=20*1024*1024;
    assert(!mach_vm_allocate(mach_task_self(),&dg_ram.address,2*1024*1024,VM_FLAGS_ANYWHERE));
    dg_ram.bytes=2*1024*1024;dg_ram.region_count=1;
    // Actual QEMU RAM is exported as a Mach memory entry before aliases or
    // GPU registrations exist. Keep the raw-allocation control distinct.
    mach_port_t memory=0;memory_object_size_t bytes=dg_ram.bytes;
    if(exported_ram)assert(!mach_make_memory_entry_64(mach_task_self(),&bytes,dg_ram.address,
        VM_PROT_READ|VM_PROT_WRITE,&memory,MACH_PORT_NULL) && bytes==dg_ram.bytes);
    dg_ram.regions[0]=(struct VIMSharedRAMRange){0x10000000,0,2*1024*1024};
    if(benchmark_only)benchmark();else{exercise(0);exercise(1);}
    if(memory)mach_port_deallocate(mach_task_self(),memory);
    VIMVRAMMemoryUnmap(&dg_vram);VIMVRAMMemoryUnmap(&dg_ram);return failures?1:0;
}
