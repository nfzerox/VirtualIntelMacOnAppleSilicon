/* Local research comparison of COMPLETE native GPU -> CPU transactions for
 * fresh fragmented backings. Same actual memory-stream decoder/interpreter,
 * GPU-current seed, rendering, immutable future seal and actual completion.
 * Interleaved ABBA, FIRST CPU bytes/padding verified after every timed Wait.
 * Context/seed/CPU initialization/validation/cleanup are outside the timer.
 * Not a desktop FPS test or a loaded-kernel CPU-gate qualification. */
#define VIM_PASSTHROUGH_OPENGL_INPROCESS 1
#define VIM_VRAM_RENDER_TARGET_EXPERIMENT 1
#define VIM_BACKING_CANONICAL_EXPERIMENT 1
#define VIM_BACKING_CANONICAL_FUTURE_EXPERIMENT 1
#define VIM_BACKING_ASYNC_EXPERIMENT 1
#define VIM_BACKING_FUTURE_EXPERIMENT 1
#define VIM_BACKING_WRITER_EXPERIMENT 1
#define VIM_BACKING_SCATTER_EXPERIMENT 1
#define VIM_BACKING_GATHER_INITIALIZE_EXPERIMENT 1
#define VIM_BACKING_COALESCING_EXPERIMENT 1
#include "../PassthroughOpenGL/passthrough-opengl-host.c"
#include "direct-opcodes.h"
#include <assert.h>
enum {BW=1236,BH=1520,BPitch=1240,BPages=(BPitch*BH*4+4095)/4096,BRAMPages=2048};
static const struct VIMGLInprocessAPI *bench_api;
static uint64_t bench_pages[BPages];
static void send_packet(void *stream,const struct DGMessage *m,const void *body)
{
    const void *buffers[]={m,body};size_t sizes[]={sizeof(*m),m->bytes};
    for(unsigned i=0;i<2;++i){
        const unsigned char *p=buffers[i];size_t left=sizes[i];
        while(left){ssize_t n=bench_api->write(stream,p,left);assert(n>0);p+=n;left-=(size_t)n;}
    }
}
static struct DGReply request(void *stream,const struct DGMessage *m,const void *body)
{
    assert(m->flags&DG_REPLY);send_packet(stream,m,body);struct DGReply r={0};
    assert(VIMGLInprocessReadExpected(bench_api,stream,&r,sizeof(r))==sizeof(r));
    unsigned char discard[4096];size_t left=r.bytes;
    while(left){size_t n=left>sizeof(discard)?sizeof(discard):left;
        assert(VIMGLInprocessReadExpected(bench_api,stream,discard,n)==(ssize_t)n);left-=n;}
    return r;
}
static void clear_color(void *stream,unsigned context,float r,float g,float b)
{
    float rgba[]={r,g,b,1};struct DGMessage m={.op=DG_OP_clear_color,.context=context};
    for(unsigned i=0;i<4;++i)memcpy(m.args+i,rgba+i,4);send_packet(stream,&m,NULL);
    m=(struct DGMessage){.op=DG_OP_clear,.context=context,.args={GL_COLOR_BUFFER_BIT}};send_packet(stream,&m,NULL);
}
static volatile uint32_t *cpu_word(size_t offset)
{return (void *)(uintptr_t)(dg_ram.address+bench_pages[offset/4096]-0x10000000+(offset&4095));}
static int number_compare(const void *a,const void *b)
{double x=*(const double *)a,y=*(const double *)b;return (x>y)-(x<y);}
int main(int argc,char **argv)
{
    int cpu_initial=0,chain=0;
    for(int i=1;i<argc;++i){
        if(!strcmp(argv[i],"--cpu-initial") && !cpu_initial)cpu_initial=1;
        else if(!strcmp(argv[i],"--chain") && !chain)chain=1;
        else {fprintf(stderr,"Usage: %s [--cpu-initial] [--chain]\n",argv[0]);return 2;}
    }
    unsigned writers=chain?8:1;
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj sentinel=NULL;GLint count=0;
    assert(!CGLChoosePixelFormat(attrs,&pf,&count) && pf);
    assert(!CGLCreateContext(pf,NULL,&sentinel) && !CGLSetCurrentContext(sentinel));
    assert(strstr((const char *)glGetString(GL_RENDERER),"Intel"));
    printf("benchmark renderer=%s fragmented_pages=%u logical=%ux%u pitch=%u\n",glGetString(GL_RENDERER),BPages,BW,BH,BPitch*4);
    printf("initial_state=%s\n",cpu_initial?"CPU-current only; initialization inside timed transaction":"GPU-current seed completed outside timed transaction");
    printf("writers_per_transaction=%u comparison=%s\n",writers,chain?"ordered-scatter vs coalesced CPU materialization":"native-RAM vs retained scatter");
    assert(!CGLDescribePixelFormat(pf,0,kCGLPFARendererID,&dg_reference_renderer));CGLDestroyPixelFormat(pf);
    dg_ram.bytes=BRAMPages*4096;dg_ram.region_count=1;
    assert(!mach_vm_allocate(mach_task_self(),&dg_ram.address,dg_ram.bytes,VM_FLAGS_ANYWHERE));
    dg_ram.regions[0]=(struct VIMSharedRAMRange){0x10000000,0,dg_ram.bytes};
    mach_port_t memory=MACH_PORT_NULL;memory_object_size_t size=dg_ram.bytes;
    assert(!mach_make_memory_entry_64(mach_task_self(),&size,dg_ram.address,
        VM_PROT_READ|VM_PROT_WRITE,&memory,MACH_PORT_NULL));
    for(unsigned i=0;i<BPages;++i)bench_pages[i]=0x10000000+(uint64_t)((i*1093)%BRAMPages)*4096;
    bench_api=VIMPassthroughOpenGLInprocessGetAPI(VIM_GL_INPROCESS_ABI,sizeof(*bench_api));assert(bench_api);
    mach_timebase_info_data_t clock;assert(!mach_timebase_info(&clock));double times[2][8];unsigned samples[2]={0};
    for(unsigned n=0;n<20;++n){
        unsigned mode=(n%4==1 || n%4==2);dg_backing_scatter=chain?1:mode;
        dg_backing_cpu_coalescing=chain?mode:1;
        void *stream=bench_api->open(),*service=bench_api->open();assert(stream && service);
        uint64_t space=n+1;struct DGMessage m={.op=DG_HELLO,.flags=DG_REPLY,.bytes=sizeof(DG_LAYOUT),
            .args={DG_MAGIC,DG_VERSION,1,0x73636174746572ULL,space}};
        struct DGReply r=request(stream,&m,DG_LAYOUT);assert(!r.error);
        m=(struct DGMessage){.op=VIM_GL_SERVICE_HELLO,.flags=DG_REPLY,
            .args={VIM_GL_SERVICE_MAGIC,VIM_GL_SERVICE_VERSION,0,0x73636174746572ULL,space}};
        r=request(service,&m,NULL);assert(!r.error);
        m=(struct DGMessage){.op=DG_CREATE,.flags=DG_REPLY};r=request(stream,&m,NULL);assert(!r.error && r.result);
        unsigned context=(unsigned)r.result;
        m=(struct DGMessage){.op=DG_RESIZE,.context=context,.flags=DG_REPLY,.args={BW,BH,1}};
        r=request(stream,&m,NULL);assert(!r.error);
        memset((void *)(uintptr_t)dg_ram.address,0xa7,dg_ram.bytes);
        for(unsigned y=0;y<BH;++y)for(unsigned x=0;x<BW;++x)*cpu_word(((size_t)y*BPitch+x)*4)=0xff000000u;
        clear_color(stream,context,0,0,0);
        m=(struct DGMessage){.op=DG_SURFACE_ACQUIRE,.context=context,.flags=DG_REPLY,.args={2000+n,1,BW,BH}};
        r=request(stream,&m,NULL);assert(!r.error && r.result);
        if(!cpu_initial){
            m=(struct DGMessage){.op=DG_SURFACE_PUBLISH,.context=context,.flags=DG_REPLY,.args={r.result,0,0,1}};
            r=request(stream,&m,NULL);assert(!r.error);
        }
        m=(struct DGMessage){.op=DG_OP_finish,.context=context,.flags=DG_REPLY};r=request(stream,&m,NULL);assert(!r.error);
        uint64_t plans=dg_scatter_stats[0],initializations=dg_gather_stats[0];uint64_t start=mach_absolute_time();
        uint64_t tickets[8]={0},copies_before=dg_scatter_stats[1],skipped_before=dg_cpu_coalescing_stats[1];
        for(unsigned writer=0;writer<writers;++writer){
            m=(struct DGMessage){.op=writer?DG_BACKING_RENDER_CONTINUE:DG_BACKING_RENDER_BEGIN,
                .context=context,.flags=DG_REPLY|DG_INPUT_ONLY,.bytes=writer?0:sizeof(bench_pages),
                .args={0,BPages*4096,BW,BH,BPitch*4,writer?tickets[writer-1]:0,0,BW,BH,0,
                    2000+n,1,writer+1,1,writer?0:BPages,writer+1}};
            r=request(stream,&m,writer?NULL:bench_pages);assert(!r.error && r.result==1);
            assert(dg_scatter_stats[0]==plans+(chain?1:mode));
            assert(dg_gather_stats[0]==initializations+(cpu_initial?(chain?1:mode):0));
            m=(struct DGMessage){.op=DG_OP_disable,.context=context,.args={GL_SCISSOR_TEST}};send_packet(stream,&m,NULL);
            clear_color(stream,context,(float)(writer+1)/writers,0,0);
            m=(struct DGMessage){.op=DG_OP_enable,.context=context,.args={GL_SCISSOR_TEST}};send_packet(stream,&m,NULL);
            m=(struct DGMessage){.op=DG_OP_scissor,.context=context,.args={17,13,BW-37,BH-29}};send_packet(stream,&m,NULL);
            clear_color(stream,context,0,0,(float)(writer+1)/writers);
            m=(struct DGMessage){.op=DG_BACKING_RENDER_DEFER,.context=context,.flags=DG_REPLY,.args={writer+1,1}};
            r=request(stream,&m,NULL);assert(!r.error && r.result);tickets[writer]=r.result;
        }
        m=(struct DGMessage){.op=DG_BACKING_COMPLETION_WAIT,.flags=DG_REPLY,.args={tickets[writers-1]}};
        r=request(service,&m,NULL);assert(!r.error && r.result==1);uint64_t end=mach_absolute_time();
        if(chain){
            assert(dg_scatter_stats[1]==copies_before+(mode?1:writers));
            assert(dg_cpu_coalescing_stats[1]==skipped_before+(mode?writers-1:0));
        }
        double ms=(double)(end-start)*clock.numer/clock.denom/1000000.0;
        if(n>=4){assert(samples[mode]<8);times[mode][samples[mode]++]=ms;}
        for(unsigned y=0;y<BH;++y)for(unsigned x=0;x<BPitch;++x){
            uint32_t expected=x>=BW?0xa7a7a7a7u:
                (x>=17 && x<BW-20 && y>=13 && y<BH-16)?0xff0000ffu:0xffff0000u;
            assert(*cpu_word(((size_t)y*BPitch+x)*4)==expected);
        }
        for(size_t i=(size_t)BPitch*BH*4;i<(size_t)BPages*4096;++i)
            assert(*(volatile unsigned char *)cpu_word(i)==0xa7);
        const char *label=chain?(mode?"coalesced":"ordered-scatter"):(mode?"scatter":"native-RAM");
        printf("SAMPLE mode=%s round=%u complete_ms=%.4f exact_first_CPU_and_padding=1\n",label,n,ms);
        for(unsigned writer=0;writer<writers;++writer){
            m=(struct DGMessage){.op=DG_BACKING_COMPLETION_RELEASE,.flags=DG_REPLY,.args={tickets[writer]}};
            r=request(service,&m,NULL);assert(!r.error && r.result==1);
        }
        assert(!bench_api->close(stream) && !bench_api->close(service));DGSnapshotCleanupDrain();
        assert(!bench_api->live_count() && !dg_backing_tickets && !dg_backing_ticket_count && !dg_surfaces && !dg_surface_bytes);
    }
    for(unsigned mode=0;mode<2;++mode){
        assert(samples[mode]==8);double sum=0;for(unsigned i=0;i<8;++i)sum+=times[mode][i];
        qsort(times[mode],8,sizeof(double),number_compare);
        printf("BENCH complete GPU/CPU ownership mode=%s samples=8 mean_ms=%.4f median_ms=%.4f max_ms=%.4f ALL exact first CPU/padding; not desktop FPS\n",
            chain?(mode?"coalesced":"ordered-scatter"):(mode?"scatter":"native-RAM"),sum/8,(times[mode][3]+times[mode][4])/2,times[mode][7]);
    }
    VIMVRAMMemoryUnmap(&dg_ram);mach_port_deallocate(mach_task_self(),memory);
    CGLSetCurrentContext(NULL);CGLDestroyContext(sentinel);return 0;
}
