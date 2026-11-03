/* Native decoder structural-negative controls: malformed containers must not
 * execute even their valid first GL command. No fake successful GL result. */
#define main DGExecutorMain
#define VIM_PASSTHROUGH_OPENGL_COMMAND_PROFILE 1
#include "../PassthroughOpenGL/passthrough-opengl-host.c"
#undef main
#include "direct-opcodes.h"
#include <assert.h>
static size_t record(unsigned char *out,uint16_t op,uint32_t context,const uint64_t *args,unsigned words)
{
    struct DGCompactScalar r={op,(uint16_t)words,context};memcpy(out,&r,sizeof(r));
    if(words)memcpy(out+sizeof(r),args,words*8);return sizeof(r)+words*8;
}
static void run(unsigned profile)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFASupportsAutomaticGraphicsSwitching,
        kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)profile,0};
    CGLPixelFormatObj format=NULL;GLint count=0;assert(!CGLChoosePixelFormat(attrs,&format,&count) && format);
    CGLContextObj context=NULL;assert(!CGLCreateContext(format,NULL,&context));CGLDestroyPixelFormat(format);
    assert(!CGLSetCurrentContext(context) && strstr((const char *)glGetString(GL_RENDERER),"Intel"));
    printf("renderer=%s profile=%x\n",glGetString(GL_RENDERER),profile);
    struct DGInterpreter *s=calloc(1,sizeof(*s));assert(s);s->hello=1;s->next=2;s->contexts[1].cgl=context;
    _Alignas(8) unsigned char storage[sizeof(struct DGMessage)+1024],good[sizeof(storage)];
    struct DGMessage *m=(void *)storage;
    memset(storage,0,sizeof(storage));*m=(struct DGMessage){.op=DG_COMPACT_SCALARS,.args={1,2}};
    uint64_t args[4]={0};float values[]={1,0,0,1};for(unsigned i=0;i<4;++i)memcpy(args+i,values+i,4);
    size_t first=record(storage+sizeof(*m),DG_OP_clear_color,1,args,4);
    m->bytes=(uint32_t)(first+record(storage+sizeof(*m)+first,DG_OP_clear_color,1,args,4));
    memcpy(good,storage,sizeof(storage));
    struct DGMessage control={.op=DG_COMMAND_PROFILE,.flags=DG_REPLY,.args={1,5000}};struct DGReply control_reply={0};
    assert(!DGCommandProfileControl(&control,&control_reply) && !control_reply.error);
    assert(DGHeaderValid(m) && !DGInterpret(s,-1,m));
    control.args[0]=2;control.args[1]=0;assert(!DGCommandProfileControl(&control,&control_reply) && !control_reply.error);
    struct DGCommandProfileRow *envelope=&dg_profile_report.commands[DGCommandProfileIndex(DG_COMPACT_SCALARS)];
    assert(envelope->calls==1 && !envelope->ticks && !envelope->maximum && envelope->bytes==m->bytes);
    assert(dg_profile_report.commands[DG_OP_clear_color].calls==2);
    puts("PASS actual profile counts decoded GL once; envelope is count/bytes only, not double-charged execution");
    GLfloat observed[4]={0};glGetFloatv(GL_COLOR_CLEAR_VALUE,observed);assert(!memcmp(observed,values,sizeof(values)) && !glGetError());
    for(unsigned test=0;test<15;++test){
        memcpy(storage,good,sizeof(storage));glClearColor(.25,.5,.75,1);
        if(test==0)m->flags=DG_REPLY;
        if(test==1)m->args[0]=2;
        if(test==2)m->args[1]=0;
        if(test==3)m->args[1]++;
        if(test==4)m->args[15]=1;
        if(test==5)m->bytes--;
        if(test==6)m->bytes++;
        if(test==7){struct DGCompactScalar r={DG_OP_clear_color,17,1};memcpy(storage+sizeof(*m)+first,&r,sizeof(r));}
        if(test==8){struct DGCompactScalar r={974,4,1};memcpy(storage+sizeof(*m)+first,&r,sizeof(r));}
        if(test==9){struct DGCompactScalar r={DG_OP_clear_color,4,0};memcpy(storage+sizeof(*m)+first,&r,sizeof(r));}
        if(test==10)m->context=1;
        if(test==11)m->bytes=DG_BATCH_BYTES;
        if(test==12)s->hello=0;
        if(test==13)s->service_only=1;
        if(test==14){struct DGCompactScalar r={DG_OP_clear_color,4,256};memcpy(storage+sizeof(*m)+first,&r,sizeof(r));}
        assert(DGInterpret(s,-1,m)==-1);
        glGetFloatv(GL_COLOR_CLEAR_VALUE,observed);
        assert(!memcmp(observed,(GLfloat[]){.25,.5,.75,1},sizeof(observed)) && !glGetError());
        s->hello=1;s->service_only=0;
    }
    puts("PASS complete-envelope validation before GL: flags/version/count/truncation/trailer/word bound/opcode/context/budget/HELLO/service; private recursion unrepresentable");
    memcpy(storage,good,sizeof(storage));
    struct DGCompactScalar missing={DG_OP_clear_color,4,2};memcpy(storage+sizeof(*m)+first,&missing,sizeof(missing));
    assert(DGInterpret(s,-1,m)==-1);glGetFloatv(GL_COLOR_CLEAR_VALUE,observed);
    assert(!memcmp(observed,values,sizeof(observed)) && !glGetError());
    puts("PASS semantic missing-context failure retains the original ordered valid prefix, never skips a command");
    CGLContextObj second=NULL;assert(!CGLCreateContext(CGLGetPixelFormat(context),NULL,&second));
    s->contexts[2].cgl=second;s->next=3;
    GLfloat green[]={0,1,0,1};uint64_t green_bits[4]={0};for(unsigned i=0;i<4;++i)memcpy(green_bits+i,green+i,4);
    record(storage+sizeof(*m)+first,DG_OP_clear_color,2,green_bits,4);
    assert(!DGInterpret(s,-1,m));
    assert(!CGLSetCurrentContext(second));glGetFloatv(GL_COLOR_CLEAR_VALUE,observed);assert(!memcmp(observed,green,sizeof(observed)) && !glGetError());
    assert(!CGLSetCurrentContext(context));glGetFloatv(GL_COLOR_CLEAR_VALUE,observed);assert(!memcmp(observed,values,sizeof(observed)) && !glGetError());
    puts("PASS one compact envelope retains actual native context identity and independent state");
    s->contexts[2].cgl=NULL;CGLDestroyContext(second);
    s->contexts[1].cgl=NULL;free(s);CGLSetCurrentContext(NULL);CGLDestroyContext(context);
}
int main(void){run(kCGLOGLPVersion_Legacy);run(kCGLOGLPVersion_3_2_Core);return 0;}
