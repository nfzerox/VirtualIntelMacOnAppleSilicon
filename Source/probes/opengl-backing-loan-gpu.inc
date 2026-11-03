/* Included by the real x64/i386 endpoint qualification. Private opt-in only:
 * the SAME kernel endpoint owns transport, selected RAM source, CPU exclusion
 * and prepared loan. No CGL/renderer replacement or cross-client lock donation.
 * Run only against an explicitly scoped-render-capable research executor. */
#include "../PassthroughOpenGL/passthrough-opengl-wire.h"
#include "direct-layout.h"
#include "direct-opcodes.h"
#include <sys/sysctl.h>
#include <uuid/uuid.h>
#define GPU_REQUIRE(x) do{if(!(x)){fprintf(stderr,"GPU loan FAIL line %d: %s\n",__LINE__,#x);return 0;}}while(0)
static int VIMLoanGPUTransfer(io_connect_t endpoint,void *buffer,size_t bytes,int writing)
{
    unsigned char *p=buffer;
    while(bytes){
        struct VIMGLTransfer request={writing?2:3,bytes>VIM_GL_CHUNK?VIM_GL_CHUNK:(uint32_t)bytes,(uintptr_t)p};
        struct VIMGLResult result={0};size_t size=sizeof(result);
        GPU_REQUIRE(!IOConnectCallStructMethod(endpoint,kIOVM2DDirectGLTransfer,&request,sizeof(request),&result,&size));
        GPU_REQUIRE(size==sizeof(result) && result.count>0 && (uint32_t)result.count<=request.bytes);
        p+=result.count;bytes-=(size_t)result.count;
    }
    return 1;
}
static int VIMLoanGPUExchange(io_connect_t endpoint,struct DGMessage *m,const void *body,struct DGReply *reply)
{
    GPU_REQUIRE(m->flags&DG_REPLY);
    GPU_REQUIRE(VIMLoanGPUTransfer(endpoint,m,sizeof(*m),1));
    if(m->bytes)GPU_REQUIRE(VIMLoanGPUTransfer(endpoint,(void*)body,m->bytes,1));
    GPU_REQUIRE(VIMLoanGPUTransfer(endpoint,reply,sizeof(*reply),0));
    GPU_REQUIRE(reply->bytes<=4096);
    unsigned char echo[4096];
    if(reply->bytes)GPU_REQUIRE(VIMLoanGPUTransfer(endpoint,echo,reply->bytes,0));
    return 1;
}
static int VIMLoanGPUClear(io_connect_t endpoint,uint32_t context,int blue)
{
    struct DGMessage m={.op=DG_OP_clear_color,.context=context};float values[]={blue?0:1,0,blue?1:0,1};
    for(unsigned i=0;i<4;++i)memcpy(m.args+i,values+i,sizeof(float));
    GPU_REQUIRE(VIMLoanGPUTransfer(endpoint,&m,sizeof(m),1));
    m=(struct DGMessage){.op=DG_OP_clear,.context=context};m.args[0]=GL_COLOR_BUFFER_BIT;
    GPU_REQUIRE(VIMLoanGPUTransfer(endpoint,&m,sizeof(m),1));return 1;
}
static int VIMQualifyBackingLoanGPU(io_connect_t *owner,io_connect_t observer)
{
    io_connect_t endpoint=*owner;
    struct DGMessage m={.op=DG_HELLO,.flags=DG_REPLY,.bytes=sizeof(DG_LAYOUT)};struct DGReply reply={0};
    m.args[0]=DG_MAGIC;m.args[1]=DG_VERSION;m.args[2]=1;
    int deferred=getenv("VIM_LOAN_GPU_DEFERRED")!=NULL;uint64_t namespace_id[2]={0,0};
    if(deferred){
        char boot[64]={0};size_t bytes=sizeof(boot);uuid_t value;
        GPU_REQUIRE(!sysctlbyname("kern.bootsessionuuid",boot,&bytes,NULL,0) && memchr(boot,0,sizeof(boot)) && !uuid_parse(boot,value));
        memcpy(namespace_id,value,16);memcpy(m.args+3,value,16);
    }
    GPU_REQUIRE(VIMLoanGPUExchange(endpoint,&m,DG_LAYOUT,&reply));
    GPU_REQUIRE(!reply.error && (reply.result&DG_CAP_BACKING_RENDER_SCOPE));
    GPU_REQUIRE(!deferred || (reply.result&DG_CAP_BACKING_COMPLETION));
    m=(struct DGMessage){.op=DG_CREATE,.flags=DG_REPLY};
    GPU_REQUIRE(VIMLoanGPUExchange(endpoint,&m,NULL,&reply) && !reply.error && reply.result);
    uint32_t context=(uint32_t)reply.result;
    for(unsigned round=0;round<3;++round){
        struct VIMSurfaceMapping mapping={0};size_t size=sizeof(mapping);
        GPU_REQUIRE(!IOConnectCallStructMethod(endpoint,kIOVM2DLockSurfaceBacking,NULL,0,&mapping,&size));
        struct VIMSurfaceRAM ram={0};uint64_t query[]={0,0};size=sizeof(ram);
        GPU_REQUIRE(!IOConnectCallMethod(endpoint,kIOVM2DGetRAMSurface,query,2,NULL,0,NULL,NULL,&ram,&size));
        struct VIMSurfaceVRAM *b=&ram.backing;
        GPU_REQUIRE(b->version==VIM_SURFACE_RAM_VERSION && ram.count==ram.total && ram.total &&
            ram.count<=VIM_SURFACE_RAM_PAGE_CHUNK && b->width==61 && b->height==37 && b->row_bytes==256 &&
            mapping.layout.offset<=mapping.length && b->length<=mapping.length-mapping.layout.offset);
        memset((void*)(uintptr_t)(mapping.address+mapping.layout.offset),0xa7,(size_t)b->length);
        struct VIMSurfaceBackingLoan loan={0};size=sizeof(loan);
        IOReturn begin_error=IOConnectCallStructMethod(endpoint,deferred?kIOVM2DBeginDeferredRAMLoan:kIOVM2DBeginRAMBackingLoan,b,sizeof(*b),&loan,&size);
        if(begin_error)fprintf(stderr,"GPU loan begin rejected round=%u deferred=%d error=%08x object=%llu incarnation=%llu epoch=%llu flags=%x\n",
            round,deferred,begin_error,(unsigned long long)b->state.object,(unsigned long long)b->state.incarnation,
            (unsigned long long)b->state.content_epoch,b->state.flags);
        GPU_REQUIRE(!begin_error);
        struct VIMSurfaceRAM pinned={0};uint64_t loan_query[]={loan.serial,0};size=sizeof(pinned);
        GPU_REQUIRE(!IOConnectCallMethod(endpoint,kIOVM2DGetRAMBackingLoan,loan_query,2,NULL,0,NULL,NULL,&pinned,&size));
        GPU_REQUIRE(!memcmp(&ram,&pinned,sizeof(ram)));
        if(!round){
            m=(struct DGMessage){.op=DG_RESIZE,.context=context,.flags=DG_REPLY};
            m.args[0]=b->width;m.args[1]=b->height;m.args[2]=1;
            GPU_REQUIRE(VIMLoanGPUExchange(endpoint,&m,NULL,&reply) && !reply.error);
        }
        m=(struct DGMessage){.op=DG_BACKING_RENDER_BEGIN,.context=context,.flags=DG_REPLY|DG_INPUT_ONLY};
        uint64_t *a=m.args;a[0]=b->offset;a[1]=b->length;a[2]=a[7]=b->width;a[3]=a[8]=b->height;
        a[4]=b->row_bytes;a[10]=b->state.object;a[11]=b->state.incarnation;a[12]=b->state.content_epoch;
        a[13]=1;a[15]=loan.serial;
        GPU_REQUIRE(VIMLoanGPUExchange(endpoint,&m,NULL,&reply) && !reply.error);
        if(reply.result==2){m.bytes=pinned.count*sizeof(uint64_t);a[14]=pinned.count;
            GPU_REQUIRE(VIMLoanGPUExchange(endpoint,&m,pinned.pages,&reply) && !reply.error);}
        GPU_REQUIRE(reply.result==1 && VIMLoanGPUClear(endpoint,context,0));
        if(round==1){
            m=(struct DGMessage){.op=DG_BACKING_RENDER_MATERIALIZE,.context=context,.flags=DG_REPLY};m.args[0]=loan.serial;
            GPU_REQUIRE(VIMLoanGPUExchange(endpoint,&m,NULL,&reply) && !reply.error && reply.result==1);
            GPU_REQUIRE(VIMLoanGPUClear(endpoint,context,1));
        }
        if(round==2){
            GPU_REQUIRE(!IOServiceClose(endpoint));*owner=0; // kernel Abort drains this same real producer
        }else{
            m=(struct DGMessage){.op=deferred?DG_BACKING_RENDER_DEFER:DG_BACKING_RENDER_END,.context=context,.flags=DG_REPLY};m.args[0]=loan.serial;
            GPU_REQUIRE(VIMLoanGPUExchange(endpoint,&m,NULL,&reply) && !reply.error && reply.result);
            struct VIMSurfaceState state={0};uint64_t end[]={loan.serial,VIM_RAM_LOAN_WRITTEN};size=sizeof(state);
            if(deferred){
                struct VIMSurfaceBackingPending pending={.serial=loan.serial,.ticket=reply.result};memcpy(pending.namespace_id,namespace_id,16);
                GPU_REQUIRE(!IOConnectCallStructMethod(endpoint,kIOVM2DCommitDeferredRAMLoan,&pending,sizeof(pending),&state,&size));
                GPU_REQUIRE(state.flags==VIM_SURFACE_STATE_GPU_PENDING);
            }else GPU_REQUIRE(reply.result==1 && !IOConnectCallMethod(endpoint,kIOVM2DEndRAMBackingLoan,end,2,NULL,0,NULL,NULL,&state,&size));
            GPU_REQUIRE(state.content_epoch==b->state.content_epoch+1);
        }
        // A different endpoint cannot read until the real owner completed End
        // or drained close. No intervening GL operation repairs CPU bytes.
        memset(&mapping,0,sizeof(mapping));size=sizeof(mapping);
        GPU_REQUIRE(!IOConnectCallStructMethod(observer,deferred?kIOVM2DWaitSurfaceBacking:kIOVM2DLockSurfaceBacking,NULL,0,&mapping,&size));
        volatile uint32_t *pixels=(void*)(uintptr_t)(mapping.address+mapping.layout.offset);
        for(unsigned y=0;y<b->height;++y)for(unsigned x=0;x<b->row_bytes/4;++x)
            GPU_REQUIRE(pixels[y*(b->row_bytes/4)+x]==(x<b->width?(round==1?0xff0000ffu:0xffff0000u):0xa7a7a7a7u));
        for(unsigned i=b->row_bytes*b->height;i<b->length;++i)GPU_REQUIRE(((volatile unsigned char*)pixels)[i]==0xa7);
        struct VIMSurfaceState state={0};uint64_t read_only=1;size=sizeof(state);
        GPU_REQUIRE(!IOConnectCallMethod(observer,kIOVM2DUnlockSurfaceBacking,&read_only,1,NULL,0,NULL,NULL,&state,&size));
        printf("PASS real kernel loan + native GPU %s: first CPU pixels/padding and returned CPU ownership\n",
            round==0?"End":round==1?"materialize/later writes":"endpoint close");
    }
    return 1;
}
#undef GPU_REQUIRE
