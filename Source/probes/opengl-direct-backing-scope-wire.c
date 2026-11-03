/* Actual memory-stream decoder and native Intel GPU, not direct helper calls.
 * Retain exported RAM until completed End/close; inspect FIRST CPU pixels
 * without a repairing readback/Finish. No VM/replacement framework needed. */
#define VIM_PASSTHROUGH_OPENGL_INPROCESS 1
#define VIM_VRAM_RENDER_TARGET_EXPERIMENT 1
#define main DGExecutorMain
#include "../PassthroughOpenGL/passthrough-opengl-host.c"
#undef main
#include "direct-opcodes.h"
#include <assert.h>
enum {W=61,H=37,Pitch=64,Pages=3,Bytes=Pages*4096};
static const struct VIMGLInprocessAPI *api;
static int test_prepared;
static void send_bytes(void *stream,const void *p,size_t bytes)
{
    while(bytes){ssize_t sent=api->write(stream,p,bytes);assert(sent>0);p=(const char*)p+sent;bytes-=(size_t)sent;}
}
static struct DGReply exchange_output(void *stream,const struct DGMessage *m,const void *body,void *output)
{
    assert(m->flags&DG_REPLY);send_bytes(stream,m,sizeof(*m));
    if(m->bytes && !(m->flags&DG_OUTPUT_ONLY))send_bytes(stream,body,m->bytes);
    struct DGReply reply={0};ssize_t received=VIMGLInprocessReadExpected(api,stream,&reply,sizeof(reply));
    if(received!=sizeof(reply))fprintf(stderr,"wire reply failed op=%x context=%u flags=%x bytes=%u received=%ld\n",
        m->op,m->context,m->flags,m->bytes,(long)received);
    assert(received==sizeof(reply));
    if(reply.bytes && output){
        assert(VIMGLInprocessReadExpected(api,stream,output,reply.bytes)==(ssize_t)reply.bytes);
    }else if(reply.bytes){
        unsigned char discarded[4096];size_t left=reply.bytes;
        while(left){size_t n=left>sizeof(discarded)?sizeof(discarded):left;
            assert(VIMGLInprocessReadExpected(api,stream,discarded,n)==(ssize_t)n);left-=n;}
    }
    return reply;
}
static struct DGReply exchange(void *stream,const struct DGMessage *m,const void *body)
{return exchange_output(stream,m,body,NULL);}
static void qualify_prepared_drawables(void)
{
    void *stream=api->open();assert(stream);
    struct DGMessage m={.op=DG_HELLO,.flags=DG_REPLY,.bytes=sizeof(DG_LAYOUT),
        .args={DG_MAGIC,DG_VERSION,1,0x7072657061726564ULL,1}};
    struct DGReply r=exchange(stream,&m,DG_LAYOUT);assert(!r.error && (r.result&DG_CAP_PREPARED_DRAWABLE));
    m=(struct DGMessage){.op=DG_CREATE,.flags=DG_REPLY};r=exchange(stream,&m,NULL);
    assert(!r.error && r.result);unsigned context=(unsigned)r.result;
    uint64_t reserved=dg_prepared_drawable_stats[0],fallbacks=dg_prepared_drawable_stats[2],unused=dg_prepared_drawable_stats[3];
    m=(struct DGMessage){.op=DG_PREPARE_DRAWABLE,.context=context,.flags=DG_REPLY,.args={W,H,1}};
    r=exchange(stream,&m,NULL);assert(!r.error && dg_prepared_drawable_stats[0]==reserved+1);
    m=(struct DGMessage){.op=DG_PREPARE_DRAWABLE,.context=context,.flags=DG_REPLY,.args={W+1,H,1}};
    r=exchange(stream,&m,NULL);assert(r.error==GL_INVALID_OPERATION);
    assert(dg_prepared_drawable_stats[2]==fallbacks); // rejected geometry must not allocate or replace
    m=(struct DGMessage){.op=DG_DROP_DRAWABLE,.context=context,.flags=DG_REPLY,.args={1}};
    r=exchange(stream,&m,NULL);assert(!r.error && dg_prepared_drawable_stats[3]==unused+1);
    m=(struct DGMessage){.op=DG_PREPARE_DRAWABLE,.context=context,.flags=DG_REPLY,.args={W,H,2}};
    r=exchange(stream,&m,NULL);assert(!r.error);
    m=(struct DGMessage){.op=DG_OP_check_framebuffer_status_EXT,.context=context,.flags=DG_REPLY,.args={GL_FRAMEBUFFER_EXT}};
    r=exchange(stream,&m,NULL);assert(!r.error && r.result==GL_FRAMEBUFFER_COMPLETE_EXT);
    assert(dg_prepared_drawable_stats[2]==fallbacks+1); // real allocation before generic observation
    m=(struct DGMessage){.op=DG_PREPARE_DRAWABLE,.context=context,.flags=DG_REPLY,.args={W,H,3}};
    r=exchange(stream,&m,NULL);assert(!r.error);
    unsigned char zero[4]={1,2,3,4};
    m=(struct DGMessage){.op=DG_READ_DRAWABLE_RECT,.context=context,.flags=DG_REPLY|DG_OUTPUT_ONLY,
        .bytes=4,.args={1,1,0,0}};
    r=exchange_output(stream,&m,NULL,zero);assert(!r.error && !zero[0] && !zero[1] && !zero[2] && !zero[3]);
    assert(dg_prepared_drawable_stats[2]==fallbacks+2); // fast-read route cannot bypass real materialization
    m=(struct DGMessage){.op=DG_OP_clear_color,.context=context};float color[]={1,0,0,1};
    for(unsigned i=0;i<4;++i)memcpy(m.args+i,color+i,4);send_bytes(stream,&m,sizeof(m));
    m=(struct DGMessage){.op=DG_OP_clear,.context=context,.args={GL_COLOR_BUFFER_BIT}};send_bytes(stream,&m,sizeof(m));
    unsigned char pixel[4]={0};m=(struct DGMessage){.op=DG_OP_read_pixels,.context=context,.flags=DG_REPLY,.bytes=4,
        .args={0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,1}};
    r=exchange_output(stream,&m,pixel,pixel);assert(!r.error && pixel[0]==255 && !pixel[1] && !pixel[2] && pixel[3]==255);
    uint64_t stats[4]={0};m=(struct DGMessage){.op=DG_VRAM_STATS,.context=context,.flags=DG_REPLY|DG_OUTPUT_ONLY,
        .bytes=sizeof(stats),.args={DG_VRAM_STATS_PREPARED_DRAWABLE}};
    r=exchange_output(stream,&m,NULL,stats);assert(!r.error);
    for(unsigned i=0;i<4;++i)assert(stats[i]==dg_prepared_drawable_stats[i]);
    assert(!api->close(stream) && !api->live_count());
    puts("PASS prepared drawable: no-allocation retirement, rejected geometry preserved, generic framebuffer/fast-read resolves before observation, exact GPU fallback pixels and typed counters");
}
static void clear(void *stream,unsigned context,float r,float g,float b)
{
    struct DGMessage m={.op=DG_OP_clear_color,.context=context};
    float color[]={r,g,b,1};for(unsigned i=0;i<4;++i)memcpy(m.args+i,color+i,sizeof(float));
    send_bytes(stream,&m,sizeof(m));m=(struct DGMessage){.op=DG_OP_clear,.context=context};
    m.args[0]=GL_COLOR_BUFFER_BIT;send_bytes(stream,&m,sizeof(m));
}
static void check_pixels(volatile uint32_t *pixels,uint32_t color)
{
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<Pitch;++x){
        if(pixels[y*Pitch+x]!=(x<W?color:0xa7a7a7a7u))
            fprintf(stderr,"first CPU mismatch x=%u y=%u actual=%08x expected=%08x\n",x,y,pixels[y*Pitch+x],x<W?color:0xa7a7a7a7u);
        assert(pixels[y*Pitch+x]==(x<W?color:0xa7a7a7a7u));
    }
    for(unsigned i=Pitch*H*4;i<Bytes;++i)assert(((volatile unsigned char*)pixels)[i]==0xa7);
}
static void triangle(void *stream,unsigned context,int extended)
{
    struct DGMessage m={.op=DG_OP_viewport,.context=context};
    m.args[2]=W;m.args[3]=H;send_bytes(stream,&m,sizeof(m));
    m=(struct DGMessage){.op=DG_OP_color4f,.context=context};
    float color[]={0,0,1,1};for(unsigned i=0;i<4;++i)memcpy(m.args+i,color+i,sizeof(float));
    send_bytes(stream,&m,sizeof(m));
    struct {struct DGVertexArray array;float vertices[6];uint32_t divisor;} body={
        .array={.slot=0,.size=2,.type=GL_FLOAT,.pointer=sizeof(struct DGVertexArray)+1,.bytes=6*sizeof(float)},
        .vertices={-1,-1,3,-1,-1,3},.divisor=0};
    m=(struct DGMessage){.op=extended?DG_DRAW_INSTANCED_ARRAYS:DG_DRAW,
        .context=context,.flags=DG_REPLY,.bytes=sizeof(body)};
    m.args[0]=GL_TRIANGLES;m.args[2]=3;m.args[6]=1;m.args[8]=1;
    if(extended){m.args[10]=offsetof(__typeof__(body),divisor)+1;m.args[11]=m.args[12]=1;}
    struct DGReply r=exchange(stream,&m,&body);assert(!r.error);
}
static void alias_pressure(void *stream,unsigned context)
{
    struct {struct DGVertexArray array;float vertices[W*H*2];} body={
        .array={.slot=0,.size=2,.type=GL_FLOAT,.pointer=sizeof(struct DGVertexArray)+1,.bytes=W*H*2*sizeof(float)}};
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
        body.vertices[(y*W+x)*2]=2*(x+.5f)/W-1;
        body.vertices[(y*W+x)*2+1]=2*(y+.5f)/H-1;
    }
    struct DGMessage m={.op=DG_OP_viewport,.context=context,.args={0,0,W,H}};
    send_bytes(stream,&m,sizeof(m));
    m=(struct DGMessage){.op=DG_DRAW,.context=context,.flags=DG_REPLY,.bytes=sizeof(body),
        .args={GL_POINTS,0,W*H,0,0,0,1,0,1}};
    for(unsigned i=0;i<32;++i){struct DGReply r=exchange(stream,&m,&body);assert(!r.error);}
}
#ifdef VIM_BACKING_ASYNC_EXPERIMENT
#include "opengl-direct-backing-defer.inc"
#endif
#ifdef VIM_BACKING_WRITER_EXPERIMENT
#include "opengl-direct-backing-writer.inc"
#endif
int main(void)
{
#ifdef VIM_BACKING_COALESCING_EXPERIMENT
    const char *coalescing=getenv("VIM_TEST_COALESCING");
    if(coalescing){assert(!strcmp(coalescing,"0") || !strcmp(coalescing,"1"));dg_backing_cpu_coalescing=!strcmp(coalescing,"1");}
#endif
    const char *prepared=getenv("VIM_TEST_PREPARED_DRAWABLE");
    if(prepared){assert(!strcmp(prepared,"0") || !strcmp(prepared,"1"));test_prepared=!strcmp(prepared,"1");}
#ifndef VIM_BACKING_CANONICAL_EXPERIMENT
    assert(!test_prepared);
#endif
#ifdef VIM_BACKING_FUTURE_EXPERIMENT
    const char *batching=getenv("VIM_TEST_BACKING_BATCHING");
    if(batching){
        assert(!strcmp(batching,"0") || !strcmp(batching,"1"));
        dg_backing_publication_batching=!strcmp(batching,"1");
    }
#endif
    CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj format=0;CGLContextObj sentinel=0;GLint count=0;
    assert(!CGLChoosePixelFormat(attributes,&format,&count) && format);
    assert(!CGLCreateContext(format,NULL,&sentinel) && !CGLSetCurrentContext(sentinel));
    assert(strstr((const char*)glGetString(GL_RENDERER),"Intel"));
    assert(!CGLDescribePixelFormat(format,0,kCGLPFARendererID,&dg_reference_renderer));
    printf("scoped wire renderer=%s\n",glGetString(GL_RENDERER));CGLDestroyPixelFormat(format);
    assert(!mach_vm_map(mach_task_self(),&dg_ram.address,16384,16383,VM_FLAGS_ANYWHERE,
        MACH_PORT_NULL,0,FALSE,VM_PROT_READ|VM_PROT_WRITE,VM_PROT_READ|VM_PROT_WRITE,VM_INHERIT_NONE));
    dg_ram.bytes=16384;dg_ram.region_count=1;
    dg_ram.regions[0]=(struct VIMSharedRAMRange){0x10000000,0,16384};
    mach_port_t memory=0;memory_object_size_t size=16384;
    assert(!mach_make_memory_entry_64(mach_task_self(),&size,dg_ram.address,VM_PROT_READ|VM_PROT_WRITE,&memory,MACH_PORT_NULL));
    uint64_t pages[]={0x10002000,0x10001000,0x10000000};struct VIMVRAMMemoryMapping view={0};
    assert(!VIMSharedRAMMapPages(&dg_ram,pages,Pages,&view));
    volatile uint32_t *pixels=(void*)(uintptr_t)view.address;
    api=VIMPassthroughOpenGLInprocessGetAPI(VIM_GL_INPROCESS_ABI,sizeof(*api));assert(api);
    if(test_prepared)qualify_prepared_drawables();
    for(unsigned mode=0;mode<7;++mode){
        void *stream=api->open();assert(stream);
        struct DGMessage m={.op=DG_HELLO,.flags=DG_REPLY,.bytes=sizeof(DG_LAYOUT)};
        m.args[0]=DG_MAGIC;m.args[1]=DG_VERSION;m.args[2]=1;
        m.args[3]=0x73636f70652d7465ULL;m.args[4]=mode+1; // explicit retained-resource namespace
        struct DGReply reply=exchange(stream,&m,DG_LAYOUT);
        assert(!reply.error && (reply.result&DG_CAP_BACKING_RENDER_SCOPE));
#ifndef VIM_BACKING_WRITER_EXPERIMENT
        assert(!(reply.result&DG_CAP_BACKING_WRITER_CONTINUATION));
#endif
        m=(struct DGMessage){.op=DG_CREATE,.flags=DG_REPLY};reply=exchange(stream,&m,NULL);
        assert(!reply.error && reply.result);unsigned context=(unsigned)reply.result;
        unsigned other=0;
        if(mode==4){
            m=(struct DGMessage){.op=DG_CREATE,.flags=DG_REPLY};m.args[0]=context;
            reply=exchange(stream,&m,NULL);assert(!reply.error && reply.result);other=(unsigned)reply.result;
        }
        m=(struct DGMessage){.op=test_prepared?DG_PREPARE_DRAWABLE:DG_RESIZE,.context=context,.flags=DG_REPLY};
        m.args[0]=W;m.args[1]=H;m.args[2]=1;reply=exchange(stream,&m,NULL);assert(!reply.error);
        for(unsigned round=0;round<4;++round){
            memset((void*)pixels,0xa7,Bytes);
            struct DGMessage begin={.op=DG_BACKING_RENDER_BEGIN,.context=context,.flags=DG_REPLY|DG_INPUT_ONLY};
            uint64_t *a=begin.args;a[1]=Bytes;a[2]=a[7]=W;a[3]=a[8]=H;a[4]=Pitch*4;
            a[10]=100+mode;a[11]=1;a[12]=round+1;a[13]=1;a[15]=round+1;
#ifdef VIM_BACKING_SCATTER_EXPERIMENT
            uint64_t scatter_before=dg_scatter_stats[3];
#endif
            reply=exchange(stream,&begin,NULL);
            if(!round){assert(!reply.error && reply.result==2);a[14]=Pages;begin.bytes=sizeof(pages);
                reply=exchange(stream,&begin,pages);}
            assert(!reply.error && reply.result==1);
#ifdef VIM_BACKING_SCATTER_EXPERIMENT
            int scatter_scope=dg_scatter_stats[3]!=scatter_before;
#endif
#ifdef VIM_BACKING_GATHER_INITIALIZE_EXPERIMENT
            if(scatter_scope && !round){
                uint32_t initial[W*H]={0};
                struct DGMessage read={.op=DG_OP_read_pixels,.context=context,.flags=DG_REPLY,.bytes=sizeof(initial),
                    .args={0,0,W,H,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,1}};
                reply=exchange_output(stream,&read,initial,initial);assert(!reply.error);
                for(unsigned i=0;i<W*H;++i)assert(initial[i]==0xa7a7a7a7u);
            }
#endif
            reply=exchange(stream,&begin,pages);assert(reply.error==GL_INVALID_OPERATION && !reply.result);
            if(mode==4 && getenv("VIM_TEST_ALIAS_PRESSURE"))alias_pressure(stream,context);
            clear(stream,context,1,0,0);
            if(mode>=5){
                uint64_t materialized=dg_vram_render_materializations;
                triangle(stream,context,mode==6);
                assert(dg_vram_render_materializations==materialized); // actual draw retains the shared FBO
            }
            m=(struct DGMessage){.op=DG_BACKING_RENDER_END,.context=context,.flags=DG_REPLY};
            m.args[0]=a[15]+1;reply=exchange(stream,&m,NULL);assert(reply.error==GL_INVALID_OPERATION);
            m=(struct DGMessage){.op=DG_RESIZE,.context=context,.flags=DG_REPLY};
            m.args[0]=W;m.args[1]=H;m.args[2]=2;reply=exchange(stream,&m,NULL);assert(reply.error==GL_INVALID_OPERATION);
            // Rejected replacement must leave the original scope active.
            uint64_t skipped=dg_vram_render_read_only_ends,republished=dg_vram_render_republished_ends;
            GLuint alias=0;
            if(mode==4){
                uint32_t zero=0;
                m=(struct DGMessage){.op=DG_OP_gen_textures,.context=context,.flags=DG_REPLY,.bytes=4};
                m.args[0]=1;m.args[1]=1;reply=exchange_output(stream,&m,&zero,&alias);assert(!reply.error && alias);
            }
            if(mode==1 || mode==3 || mode==4){
                m=(struct DGMessage){.op=DG_BACKING_RENDER_MATERIALIZE,.context=context,.flags=DG_REPLY};m.args[0]=a[15];
                reply=exchange(stream,&m,NULL);assert(!reply.error && reply.result==1);
                reply=exchange(stream,&m,NULL);assert(!reply.error && reply.result==1);
                if(mode==1)clear(stream,context,0,0,1); // later private writes MUST be published
                if(mode==4){
                    // A different native sharing context writes the source
                    // through its live texture alias. Owner damage alone
                    // cannot observe this mutation; group tracking must.
                    m=(struct DGMessage){.op=DG_OP_bind_texture,.context=context};
                    m.args[0]=GL_TEXTURE_RECTANGLE_ARB;m.args[1]=alias;send_bytes(stream,&m,sizeof(m));
                    m=(struct DGMessage){.op=DG_BIND_DRAWABLE_TEXTURE,.context=context,.flags=DG_REPLY};
                    m.args[0]=GL_RGBA8;m.args[1]=W;m.args[2]=H;reply=exchange(stream,&m,NULL);assert(!reply.error);
                    m=(struct DGMessage){.op=DG_OP_bind_texture,.context=other};
                    m.args[0]=GL_TEXTURE_RECTANGLE_ARB;m.args[1]=alias;send_bytes(stream,&m,sizeof(m));
                    uint32_t blue[W*H];for(unsigned i=0;i<W*H;++i)blue[i]=0xff0000ffu;
                    m=(struct DGMessage){.op=DG_OP_tex_sub_image2D,.context=other,.flags=DG_REPLY,.bytes=sizeof(blue)};
                    m.args[0]=GL_TEXTURE_RECTANGLE_ARB;m.args[4]=W;m.args[5]=H;
                    m.args[6]=GL_BGRA;m.args[7]=GL_UNSIGNED_INT_8_8_8_8_REV;m.args[8]=1;
                    reply=exchange(stream,&m,blue);assert(!reply.error);
                    m=(struct DGMessage){.op=DG_FLUSH_DRAWABLE_STATE,.context=other,.flags=DG_REPLY};
                    reply=exchange(stream,&m,NULL);assert(!reply.error);
                }
            }
            if(mode==2 && round==3){
                assert(!api->close(stream));stream=NULL; // no End: actual native retirement must drain
            }else{
                m=(struct DGMessage){.op=DG_BACKING_RENDER_END,.context=context,.flags=DG_REPLY};m.args[0]=a[15];
                reply=exchange(stream,&m,NULL);assert(!reply.error && reply.result==1);
            }
            check_pixels(pixels,(mode==1 || mode==4 || mode>=5)?0xff0000ffu:0xffff0000u);
            if(mode==3){
#ifdef VIM_BACKING_SCATTER_EXPERIMENT
                if(scatter_scope){
                    // Rendering was in canonical GPU storage, NOT guest RAM.
                    // An early GPU materialization cannot waive its still-
                    // required FIRST CPU copy at End. Pixel checks above stay.
                    assert(dg_vram_render_read_only_ends==skipped && dg_vram_render_republished_ends==republished+1);
                }else
#endif
                    assert(dg_vram_render_read_only_ends==skipped+1 && dg_vram_render_republished_ends==republished);
            }
            if(mode==1 || mode==4)assert(dg_vram_render_read_only_ends==skipped && dg_vram_render_republished_ends==republished+1);
            assert(CGLGetCurrentContext()==sentinel);
            if(stream){
#ifdef VIM_BACKING_CANONICAL_EXPERIMENT
                struct DGMessage resource={.op=DG_SURFACE_ACQUIRE,.context=context,.flags=DG_REPLY};
                resource.args[0]=a[10];resource.args[1]=a[11];resource.args[2]=W;resource.args[3]=H;
                reply=exchange(stream,&resource,NULL);assert(!reply.error && reply.result);
                uint32_t lease=(uint32_t)reply.result;uint64_t copies=dg_scope_canonical_stats[2];
                resource=(struct DGMessage){.op=DG_SURFACE_PUBLISH,.context=context,.flags=DG_REPLY};
                resource.args[0]=lease;resource.args[3]=a[12]+1;
                reply=exchange(stream,&resource,NULL);assert(!reply.error);
                assert(dg_scope_canonical_stats[2]==copies+1); // same backing: actual publication is NOT another blit
                uint64_t canonical[4]={0};
                resource=(struct DGMessage){.op=DG_VRAM_STATS,.context=context,
                    .flags=DG_REPLY|DG_OUTPUT_ONLY,.bytes=sizeof(canonical)};
                resource.args[0]=DG_VRAM_STATS_CANONICAL_SCOPE;
                reply=exchange_output(stream,&resource,NULL,canonical);
                assert(!reply.error && canonical[0] && canonical[2]==copies+1);
                struct DGMessage stale=begin;stale.args[15]+=100;
                uint64_t rejected=dg_scope_canonical_stats[3];
                reply=exchange(stream,&stale,pages);assert(!reply.error && !reply.result);
                assert(dg_scope_canonical_stats[3]==rejected+(alias?0:1));
                // Existing live aliases reject BEFORE the epoch guard; in
                // either case old CPU pixels cannot replace the new version.
#endif
                uint64_t stats[4]={0};
                struct DGMessage stats_request={.op=DG_VRAM_STATS,.context=context,
                    .flags=DG_REPLY|DG_OUTPUT_ONLY,.bytes=sizeof(stats)};
                stats_request.args[0]=DG_VRAM_STATS_BACKING_SCOPE;
                reply=exchange_output(stream,&stats_request,NULL,stats);
                assert(!reply.error && stats[0]==dg_vram_render_begins &&
                    stats[1]==dg_vram_render_materializations &&
                    stats[2]==dg_vram_render_read_only_ends && stats[3]==dg_vram_render_republished_ends);
                stats_request.args[0]=DG_VRAM_STATS_DRAWABLE_STORAGE;
                reply=exchange_output(stream,&stats_request,NULL,stats);
                assert(!reply.error);
                for(unsigned i=0;i<4;++i)
                    assert(stats[i]==__atomic_load_n(&dg_drawable_registration_stats[i],__ATOMIC_RELAXED));
                reply=exchange(stream,&m,NULL);assert(reply.error==GL_INVALID_OPERATION); // stale End
                reply=exchange(stream,&begin,pages);assert(reply.error==GL_INVALID_OPERATION); // stale Begin
                if(alias){
                    m=(struct DGMessage){.op=DG_OP_bind_texture,.context=other};
                    m.args[0]=GL_TEXTURE_RECTANGLE_ARB;send_bytes(stream,&m,sizeof(m));
                    m=(struct DGMessage){.op=DG_OP_delete_textures,.context=context,.flags=DG_REPLY,.bytes=4};
                    m.args[0]=1;m.args[1]=1;reply=exchange(stream,&m,&alias);assert(!reply.error);
                }
#ifdef VIM_BACKING_CANONICAL_EXPERIMENT
                if(mode==0 && round==3){
                    // An ordinary retained-family attachment must revoke
                    // scoped-canonical eligibility, even on this drawable.
                    struct DGMessage foreign={.op=DG_SURFACE_ACQUIRE,.context=context,.flags=DG_REPLY};
                    foreign.args[0]=900;foreign.args[1]=1;foreign.args[2]=W;foreign.args[3]=H;
                    reply=exchange(stream,&foreign,NULL);assert(!reply.error && reply.result);
                    foreign=(struct DGMessage){.op=DG_SURFACE_DRAWABLE,.context=context,
                        .flags=DG_REPLY,.args={reply.result}};
                    reply=exchange(stream,&foreign,NULL);assert(!reply.error);
                    struct DGMessage rejected_begin=begin;
                    rejected_begin.args[12]=100;rejected_begin.args[15]=1000;
                    uint64_t attachments=dg_scope_canonical_stats[0],reuses=dg_scope_canonical_stats[1];
                    reply=exchange(stream,&rejected_begin,pages);assert(!reply.error && !reply.result);
                    assert(dg_scope_canonical_stats[0]==attachments && dg_scope_canonical_stats[1]==reuses);
                }
#endif
            }
        }
        if(stream)assert(!api->close(stream));assert(!api->live_count());
#ifdef VIM_BACKING_CANONICAL_EXPERIMENT
        assert(!dg_surfaces && !dg_surface_leases && !dg_surface_count && !dg_surface_bytes);
#endif
        printf("PASS scoped wire %s: real GPU writes, first CPU pixels/padding, stale serials, exclusion and metadata reuse\n",
            mode==0?"completed End":mode==1?"materialize + later writes":mode==2?"close without End":
                mode==3?"read-only materialization preserves required CPU completion":mode==4?"shared-context alias writes still publish":
                mode==5?"array triangles stay in shared storage":"instanced triangles stay in shared storage");
    }
#ifdef VIM_BACKING_GATHER_INITIALIZE_EXPERIMENT
    assert(dg_scoped_alias_cpu_waits>=4);
    printf("PASS actual scoped alias CPU-upload boundary: completed producer dependencies=%llu, original first CPU pixels retained\n",
        (unsigned long long)dg_scoped_alias_cpu_waits);
#endif
#ifdef VIM_BACKING_ASYNC_EXPERIMENT
    qualify_deferred_backing(pages,pixels,sentinel);
#endif
#ifdef VIM_BACKING_WRITER_EXPERIMENT
    qualify_backing_writers(pages,pixels,sentinel);
#endif
#ifdef VIM_BACKING_FUTURE_EXPERIMENT
    assert(dg_backing_publication_batching?dg_backing_batched_publications>0:!dg_backing_batched_publications);
    printf("PASS compound future publication batching=%d actual compound seals=%llu, immutable versions and RAM completion retained\n",
        dg_backing_publication_batching,(unsigned long long)dg_backing_batched_publications);
#endif
#ifdef VIM_BACKING_SCATTER_EXPERIMENT
    uint64_t required_copies=64;
#ifdef VIM_BACKING_COALESCING_EXPERIMENT
    if(dg_backing_cpu_coalescing && !getenv("VIM_TEST_COALESCING_VETO")){
        assert(dg_cpu_coalescing_stats[0] && dg_cpu_coalescing_stats[1]>=63 && dg_cpu_coalescing_stats[2]);
        required_copies-=63;
        printf("PASS actual CPU coalescing: groups=%llu skipped copies=%llu avoided bytes=%llu forwarded waits=%llu; covered CPU writes complete only AFTER newest actual scatter\n",
            (unsigned long long)dg_cpu_coalescing_stats[0],(unsigned long long)dg_cpu_coalescing_stats[1],
            (unsigned long long)dg_cpu_coalescing_stats[2],(unsigned long long)dg_cpu_coalescing_stats[3]);
    }
#endif
    assert(dg_scatter_stats[0] && dg_scatter_stats[1]>=required_copies && dg_scatter_stats[2] && dg_scatter_stats[3]>=64);
    printf("PASS actual scatter ownership route: plans=%llu CPU-complete copies=%llu bytes=%llu admitted scopes=%llu; no native fragmented RAM registration\n",
        (unsigned long long)dg_scatter_stats[0],(unsigned long long)dg_scatter_stats[1],
        (unsigned long long)dg_scatter_stats[2],(unsigned long long)dg_scatter_stats[3]);
#endif
#ifdef VIM_BACKING_GATHER_INITIALIZE_EXPERIMENT
    assert(dg_gather_stats[0]>=7 && dg_gather_stats[1] && !dg_gather_stats[3]);
    printf("PASS CPU-current initialization before first GPU version: initialized=%llu bytes=%llu, actual initial GPU pixels, known/stale/pending epochs still guarded\n",
        (unsigned long long)dg_gather_stats[0],(unsigned long long)dg_gather_stats[1]);
#endif
    VIMVRAMMemoryUnmap(&view);mach_port_deallocate(mach_task_self(),memory);VIMVRAMMemoryUnmap(&dg_ram);
    assert(!CGLSetCurrentContext(NULL));CGLDestroyContext(sentinel);return 0;
}
