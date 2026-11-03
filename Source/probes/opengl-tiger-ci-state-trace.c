/* Finite OWN-PROCESS 8R4088 CI heap watch. Never ship as a driver or inject
 * into a system process. The measured CI ivars identify the native saved
 * texture word; a read-only PRIVATE heap page plus single-step observes its
 * writers without changing native code, state, return values or GL behavior.
 * Load only through the public CI fixture's explicit trace-library option. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/CGLContext.h>
#include <OpenGL/glext.h>
#include <objc/objc-runtime.h>
#include <objc/objc-class.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/ucontext.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <mach-o/dyld.h>
#include <stddef.h>
#include "../PassthroughOpenGL/tiger-rosetta-platform.h"
#include "../PassthroughOpenGL/tiger-coreimage-state.c"
static volatile uint32_t *word;
static uintptr_t page;
static size_t page_size;
static uint32_t last;
static unsigned changes;
static volatile sig_atomic_t stepping;
static uintptr_t writer;
static unsigned char alternate_stack[65536];
/* Exact native10.4 context callback consumed by fe_gl_load_programs. This
 * finite OWN-PROCESS observation leaves all query results/errors untouched. */
_Static_assert(offsetof(struct _CGLContextObject,disp.get_programiv_ARB)==0x854,
    "measured Tiger program-query callback");
struct ProgramTrace {
    CGLContextObj context;
    GLIContext renderer;
    void (*query)(GLIContext,GLenum,GLenum,GLint *);
    void (*source)(GLIContext,GLenum,GLenum,void *);
    void (*integer)(GLIContext,GLenum,GLint *);
    const GLubyte *(*string)(GLIContext,GLenum);
};
static struct ProgramTrace program_trace[2];
static unsigned program_trace_count,program_trace_budget=12;
static void ProgramQuery(GLIContext opaque,GLenum target,GLenum key,GLint *value)
{
    struct ProgramTrace *trace=NULL;
    for(unsigned i=0;i<program_trace_count;++i)
        if(program_trace[i].renderer==opaque){trace=program_trace+i;break;}
    if(!trace)_exit(77);
    trace->query(opaque,target,key,value);
    if(key!=GL_PROGRAM_UNDER_NATIVE_LIMITS_ARB||!program_trace_budget)return;
    --program_trace_budget;
    GLint length=0,name=0,position=-2;
    trace->query(opaque,target,GL_PROGRAM_LENGTH_ARB,&length);
    trace->query(opaque,target,GL_PROGRAM_BINDING_ARB,&name);
    trace->integer(opaque,GL_PROGRAM_ERROR_POSITION_ARB,&position);
    fprintf(stderr,"CI PROGRAM target=%x name=%d native=%d position=%d length=%d\n",
        (unsigned)target,(int)name,value?(int)*value:-1,(int)position,(int)length);
    const GLenum keys[]={GL_PROGRAM_INSTRUCTIONS_ARB,GL_PROGRAM_NATIVE_INSTRUCTIONS_ARB,
        GL_PROGRAM_TEMPORARIES_ARB,GL_PROGRAM_NATIVE_TEMPORARIES_ARB,
        GL_PROGRAM_PARAMETERS_ARB,GL_PROGRAM_NATIVE_PARAMETERS_ARB,
        GL_PROGRAM_ALU_INSTRUCTIONS_ARB,GL_PROGRAM_TEX_INSTRUCTIONS_ARB,
        GL_PROGRAM_TEX_INDIRECTIONS_ARB};
    for(unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);++i){
        if(i>=6&&target!=GL_FRAGMENT_PROGRAM_ARB)break;
        GLint actual=0;trace->query(opaque,target,keys[i],&actual);
        fprintf(stderr,"CI PROGRAM query=%x value=%d\n",(unsigned)keys[i],(int)actual);
    }
    if(length>0&&length<=32768){
        char *text=malloc((size_t)length);
        if(text){trace->source(opaque,target,GL_PROGRAM_STRING_ARB,text);
            fputs("CI PROGRAM SOURCE BEGIN\n",stderr);fwrite(text,1,(size_t)length,stderr);
            fputs("\nCI PROGRAM SOURCE END\n",stderr);free(text);}
    }
}
static void TraceProgramQueries(CGLContextObj context)
{
    if(!context)return;
    for(unsigned i=0;i<program_trace_count;++i)if(program_trace[i].context==context)return;
    if(program_trace_count==2||!context->disp.get_programiv_ARB||
       !context->disp.get_program_string_ARB||!context->disp.get_integerv||!context->disp.get_string)_exit(77);
    struct ProgramTrace *trace=program_trace+program_trace_count++;
    trace->context=context;trace->renderer=context->rend;trace->query=context->disp.get_programiv_ARB;
    trace->source=context->disp.get_program_string_ARB;trace->integer=context->disp.get_integerv;
    trace->string=context->disp.get_string;context->disp.get_programiv_ARB=ProgramQuery;
    fprintf(stderr,"CI PROGRAM observing own context=%p\n",(void *)context);
}
static char *Hex(char *p,uintptr_t n)
{
    static const char digits[]="0123456789abcdef";
    *p++=' ';for(int i=7;i>=0;--i)*p++=digits[(n>>(i*4))&15];return p;
}
static void Record(x86_thread_state32_t *s)
{
    uint32_t value=*word;if(value==last)return;
    char line[256],*out=line;memcpy(out,"CI watch",8);out+=8;
    out=Hex(out,writer);out=Hex(out,s->eip);out=Hex(out,last);out=Hex(out,value);
    out=Hex(out,s->eax);out=Hex(out,s->edx);out=Hex(out,s->esi);out=Hex(out,s->edi);
    uintptr_t frame=s->ebp;
    for(unsigned i=0;i<5 && frame>=s->esp && frame-s->esp<65536;++i){
        out=Hex(out,((uintptr_t *)frame)[1]);frame=*(uintptr_t *)frame;
    }
    *out++='\n';write(STDERR_FILENO,line,(size_t)(out-line));
    last=value;if(++changes>=64){mprotect((void *)page,page_size,PROT_READ|PROT_WRITE);word=NULL;}
}
static void Trap(int signal,siginfo_t *info,void *opaque)
{
    x86_thread_state32_t *s=&((ucontext_t *)opaque)->uc_mcontext->ss;
    if(signal==SIGTRAP&&stepping){
        stepping=0;s->eflags&=~0x100U;if(word)Record(s);
        if(word&&mprotect((void *)page,page_size,PROT_READ))_exit(78);
        return;
    }
    uintptr_t fault=(uintptr_t)info->si_addr;
    if((signal==SIGSEGV||signal==SIGBUS)&&word&&fault>=page&&fault-page<page_size){
        if(mprotect((void *)page,page_size,PROT_READ|PROT_WRITE))_exit(78);
        writer=s->eip;stepping=1;s->eflags|=0x100;return;
    }
    _exit(78);
}
void VIMTraceContext(CGLContextObj unused)
{
    (void)unused;
    if(getenv("VIM_CI_FIX_STATE")){
        int fixed=0;
        for(uint32_t i=0;i<_dyld_image_count();++i){
            const char *path=_dyld_get_image_name(i);
            if(path&&strstr(path,"/QuartzCore.framework/")){
                fixed=VIMTigerFixCIState(_dyld_get_image_header(i),_dyld_get_image_vmaddr_slide(i),path);break;
            }
        }
        fprintf(stderr,"CI native program-state alias fixed=%d\n",fixed);
        if(!fixed)_exit(77);
    }
    if(getenv("VIM_CI_PROGRAM_QUERY"))TraceProgramQueries(unused);
}
void VIMTraceCIContext(void *opaque)
{
    int policy=getenv("VIM_CI_POLICY_QUERY")!=NULL;
    int watch=getenv("VIM_CI_WATCH_STATE")!=NULL;
    int programs=getenv("VIM_CI_PROGRAM_QUERY")!=NULL;
    if(!policy&&!watch&&!programs)return;
    id context=(id)opaque,impl=*(id *)((char *)context+4);
    fprintf(stderr,"CI object=%s impl=%s\n",context->isa->name,impl?impl->isa->name:"none");
    if(!impl||strcmp(impl->isa->name,"CIOpenGLContextImpl"))_exit(77);
    objc_msgSend(impl,sel_registerName("updateContext"));
    void *fe=((void *(*)(id,SEL))objc_msgSend)(impl,sel_registerName("_feContext"));
    if(!fe){fputs("CI FE not initialized\n",stderr);_exit(77);}
    if(policy){
        /* Read-only own-process policy oracle. Native10.4 updateContext
         * sets bit0x8 when its0x44 FE context is accelerated; only that GL
         * context owns the0x7c accelerator pointer. Never interpret the
         * software context using the GL layout after admission failed. */
        unsigned accelerated=!!(((unsigned char *)impl)[0xc0]&8);
        fprintf(stderr,"CI policy accelerated=%u\n",accelerated);
        if(accelerated){
            int (*get)(void *,unsigned,unsigned)=NULL;
            for(uint32_t i=0;i<_dyld_image_count();++i){
                const char *path=_dyld_get_image_name(i);
                if(!path||!strstr(path,"/QuartzCore.framework/")||
                   strcmp(strrchr(path,'/'),"/QuartzCore"))continue;
                struct nlist symbol[2];memset(symbol,0,sizeof(symbol));
                symbol[0].n_un.n_name="_fe_accel_get";
                if(nlist(path,symbol)||(symbol[0].n_type&N_TYPE)!=N_SECT)break;
                unsigned char *entry=(void *)(_dyld_get_image_vmaddr_slide(i)+symbol[0].n_value);
                static const unsigned char prologue[]={0x55,0x89,0xe5,0x53,0xe8};
                if(!memcmp(entry,prologue,sizeof(prologue)))get=(void *)entry;
                break;
            }
            void *accel=*(void **)((char *)fe+0x7c);
            if(!get||!accel){fputs("CI policy interface not matched\n",stderr);_exit(77);}
            fprintf(stderr,"CI policy class=%d speed=%d software=%d vertex=%d fragment=%d float=%d\n",
                get(accel,1,0),get(accel,9,0),get(accel,8,0),
                get(accel,3,0),get(accel,4,0),get(accel,5,0));
        }
        if(!watch&&!programs)return;
    }
    if(programs){
        if(!(((unsigned char *)impl)[0xc0]&8)){fputs("CI PROGRAM no accelerated FE context\n",stderr);_exit(77);}
        CGLContextObj (*get_context)(void *)=NULL;
        for(uint32_t i=0;i<_dyld_image_count();++i){
            const char *path=_dyld_get_image_name(i);
            if(!path||!strstr(path,"/QuartzCore.framework/")||strcmp(strrchr(path,'/'),"/QuartzCore"))continue;
            struct nlist symbol[2];memset(symbol,0,sizeof(symbol));symbol[0].n_un.n_name="_fe_gl_current_context";
            if(nlist(path,symbol)||(symbol[0].n_type&N_TYPE)!=N_SECT)break;
            unsigned char *entry=(void *)(_dyld_get_image_vmaddr_slide(i)+symbol[0].n_value);
            static const unsigned char prologue[]={0x55,0x89,0xe5,0x57,0x56,0x83,0xec,0x20};
            if(!memcmp(entry,prologue,sizeof(prologue)))get_context=(void *)entry;
            break;
        }
        if(!get_context){fputs("CI PROGRAM current-context interface not matched\n",stderr);_exit(77);}
        TraceProgramQueries(get_context(fe));
        if(!watch)return;
    }
    word=(volatile uint32_t *)((char *)fe+0x1d4);last=*word;
    page_size=(size_t)getpagesize();page=(uintptr_t)word&~(page_size-1);
    fprintf(stderr,"CI watch FE=%p backup=%p initial=%x\n",fe,word,last);
    stack_t stack={alternate_stack,sizeof(alternate_stack),0};
    if(sigaltstack(&stack,NULL))_exit(77);
    struct sigaction action;memset(&action,0,sizeof(action));
    action.sa_sigaction=Trap;action.sa_flags=SA_SIGINFO|SA_ONSTACK;sigemptyset(&action.sa_mask);
    if(sigaction(SIGTRAP,&action,NULL)||sigaction(SIGSEGV,&action,NULL)||sigaction(SIGBUS,&action,NULL))_exit(77);
    if(mprotect((void *)page,page_size,PROT_READ))_exit(77);
}
void VIMTraceCIEnd(void)
{
    if(word)mprotect((void *)page,page_size,PROT_READ|PROT_WRITE);word=NULL;
}
