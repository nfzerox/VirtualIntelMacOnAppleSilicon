/* Actual decoder + native GPU controls for the compile-only diagnostic.
 * No VM, framework replacement or host security changes are required. */
#define VIM_PASSTHROUGH_OPENGL_INPROCESS 1
#define VIM_PASSTHROUGH_OPENGL_COMMAND_PROFILE 1
#define main DGExecutorMain
#include "../PassthroughOpenGL/passthrough-opengl-host.c"
#undef main
#include "direct-opcodes.h"
#include <assert.h>
static const struct VIMGLInprocessAPI *api;
static struct DGReply command(void *stream,struct DGMessage *m,void *body)
{
    assert(api->write(stream,m,sizeof(*m))==sizeof(*m));
    if(m->bytes && !(m->flags&DG_OUTPUT_ONLY))assert(api->write(stream,body,m->bytes)==(ssize_t)m->bytes);
    struct DGReply r={0};assert(VIMGLInprocessReadExpected(api,stream,&r,sizeof(r))==sizeof(r));
    assert(r.bytes==((m->flags&DG_INPUT_ONLY)?0:m->bytes));
    if(r.bytes)assert(body && VIMGLInprocessReadExpected(api,stream,body,r.bytes)==(ssize_t)r.bytes);
    return r;
}
static struct DGReply control(void *stream,unsigned action,unsigned duration,void *out)
{
    struct DGMessage m={.op=DG_COMMAND_PROFILE,.flags=DG_REPLY,.args={action,duration}};
    if(action>=3){m.flags|=DG_OUTPUT_ONLY;
        m.bytes=action==3?sizeof(struct DGCommandProfileReport):action==4?sizeof(struct DGSnapshotImportPolicyReport):sizeof(struct DGSnapshotRecyclePolicyReport);}
    return command(stream,&m,out);
}
int main(void)
{
    /* An offline-capable pixel format may otherwise select the discrete GPU
     * even with gpuswitch=0. Positively choose/verify the Intel oracle first. */
    CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj format=NULL;CGLContextObj native=NULL;GLint formats=0,screen=0,renderer=0;
    assert(!CGLChoosePixelFormat(attributes,&format,&formats) && format);
    assert(!CGLCreateContext(format,NULL,&native) && !CGLSetCurrentContext(native));
    assert(strstr((const char *)glGetString(GL_RENDERER),"Intel"));
    assert(!CGLGetVirtualScreen(native,&screen) && !CGLDescribePixelFormat(format,screen,kCGLPFARendererID,&renderer));
    char selected[32];snprintf(selected,sizeof(selected),"0x%x",renderer);setenv("VIM_PASSTHROUGH_OPENGL_RENDERER_ID",selected,1);
    CGLSetCurrentContext(NULL);CGLDestroyContext(native);CGLDestroyPixelFormat(format);
    api=VIMPassthroughOpenGLInprocessGetAPI(VIM_GL_INPROCESS_ABI,sizeof(*api));assert(api);
    void *stream=api->open();assert(stream);
    struct DGMessage m={.op=DG_HELLO,.flags=DG_REPLY,.bytes=sizeof(DG_LAYOUT),.args={DG_MAGIC,DG_VERSION,1}};
    char layout[]=DG_LAYOUT;struct DGReply r=command(stream,&m,layout);
    assert(!r.error && (r.result&DG_CAP_COMMAND_PROFILE) && (r.result&DG_CAP_SNAPSHOT_IMPORT_POLICY) && (r.result&DG_CAP_SNAPSHOT_RECYCLE_POLICY));
    r=control(stream,1,10000,NULL);assert(!r.error && r.result);uint64_t first=r.result;
    r=control(stream,1,10000,NULL);assert(r.error==GL_INVALID_OPERATION && r.result==first);
    struct DGCommandProfileReport *report=calloc(1,sizeof(*report));assert(report);
    r=control(stream,3,0,report);assert(r.error==GL_INVALID_OPERATION);
    struct DGSnapshotImportPolicyReport policy={0};
    r=control(stream,4,0,&policy);assert(r.error==GL_INVALID_OPERATION);
    struct DGSnapshotRecyclePolicyReport recycling={0};
    assert((r=control(stream,5,0,&recycling)).error==GL_INVALID_OPERATION);
    m=(struct DGMessage){.op=DG_CREATE,.flags=DG_REPLY};r=command(stream,&m,NULL);
    assert(!r.error && r.result);unsigned context=(unsigned)r.result;
    char name[4096]={0};m=(struct DGMessage){.op=DG_STRING,.context=context,.flags=DG_REPLY,.bytes=sizeof(name),.args={GL_RENDERER,1}};
    r=command(stream,&m,name);assert(!r.error && strstr(name,"Intel"));printf("actual_wire_renderer=%s\n",name);
    m=(struct DGMessage){.op=DG_RESIZE,.context=context,.flags=DG_REPLY,.args={32,32,1}};
    r=command(stream,&m,NULL);assert(!r.error);
    m=(struct DGMessage){.op=DG_OP_clear_color,.context=context};
    float red[]={1,0,0,1};for(unsigned i=0;i<4;++i)memcpy(m.args+i,red+i,4);
    assert(api->write(stream,&m,sizeof(m))==sizeof(m));
    m=(struct DGMessage){.op=DG_OP_clear,.context=context,.args={GL_COLOR_BUFFER_BIT}};
    assert(api->write(stream,&m,sizeof(m))==sizeof(m));
    unsigned char pixel[4]={0};
    m=(struct DGMessage){.op=DG_OP_read_pixels,.context=context,.flags=DG_REPLY,.bytes=4,
        .args={0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,1}};
    r=command(stream,&m,pixel);assert(!r.error && pixel[0]==255 && !pixel[1] && !pixel[2] && pixel[3]==255);
    m=(struct DGMessage){.op=DG_ERROR,.context=context,.flags=DG_REPLY};r=command(stream,&m,NULL);
    assert(!r.error && !r.result);
    r=control(stream,2,0,NULL);assert(!r.error && r.result==first);
    r=control(stream,3,0,report);assert(!r.error && report->version==1 && report->generation==first);
    assert(report->numer && report->denom && report->ended>report->started);
    assert(report->commands[DG_OP_clear].calls==1 && report->commands[DG_OP_read_pixels].calls==1);
    assert(report->commands[DGCommandProfileIndex(DG_CREATE)].calls==1);
    assert(report->commands[DG_OP_clear].ticks>=report->commands[DG_OP_clear].maximum);
    assert(!strcmp(getenv("VIM_PASSTHROUGH_OPENGL_INLINE")?getenv("VIM_PASSTHROUGH_OPENGL_INLINE"):"0","1")?report->batches.calls>0:report->batches.calls==0);
    printf("PASS exact wire counts and first native GPU pixels; generation=%llu batches=%llu excluded=%llu\n",
        (unsigned long long)first,(unsigned long long)report->batches.calls,(unsigned long long)report->excluded);
    r=control(stream,1,1,NULL);assert(!r.error && r.result>first);uint64_t second=r.result;
    usleep(5000);
    m=(struct DGMessage){.op=DG_ERROR,.context=context,.flags=DG_REPLY};r=command(stream,&m,NULL);assert(!r.error);
    r=control(stream,3,0,report);assert(!r.error && report->generation==second && report->ended>report->started);
    assert(report->excluded && !report->commands[DGCommandProfileIndex(DG_ERROR)].calls);
    puts("PASS finite expiry, reset, duplicate-arm/read-while-active refusal and crossing-boundary exclusion; no rendering policy changed");
    r=control(stream,4,UINT32_MAX,&policy);assert(!r.error && policy.version==1 && policy.flags==3);
    r=control(stream,4,0,&policy);assert(!r.error && !policy.flags);
    r=control(stream,4,4,&policy);assert(r.error==GL_INVALID_VALUE);
    r=control(stream,4,UINT32_MAX,&policy);assert(!r.error && !policy.flags);
    r=control(stream,4,3,&policy);assert(!r.error && policy.flags==3);
    puts("PASS explicit research-only snapshot policy, readback, active-profile/invalid-value refusal and restored defaults");
    assert((r=control(stream,5,UINT32_MAX,&recycling)).error==0 && recycling.version==1 && recycling.flags==1);
    assert((r=control(stream,5,0,&recycling)).error==0 && !recycling.flags);
    assert((r=control(stream,5,2,&recycling)).error==GL_INVALID_VALUE);
    assert((r=control(stream,5,UINT32_MAX,&recycling)).error==0 && !recycling.flags);
    assert((r=control(stream,5,1,&recycling)).error==0 && recycling.flags==1);
    puts("PASS separately negotiated consumer-recycle policy, active/invalid refusal and restored defaults");
    free(report);assert(!api->close(stream) && !api->live_count());return 0;
}
