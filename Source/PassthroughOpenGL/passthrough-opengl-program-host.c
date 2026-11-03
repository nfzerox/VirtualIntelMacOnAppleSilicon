/* Native ARB assembly programs execute in the existing host GL context.
 * No shader translation or compositor-specific capability patch lives here. */
#include "arb-program-source.h"
static pthread_once_t dg_arb_cookie_once=PTHREAD_ONCE_INIT;
static char dg_arb_cookie[33];
static void DGARBInitializeCookie(void)
{
    unsigned char bytes[16];arc4random_buf(bytes,sizeof(bytes));
    static const char hex[]="0123456789abcdef";
    for(unsigned i=0;i<16;++i){dg_arb_cookie[2*i]=hex[bytes[i]>>4];dg_arb_cookie[2*i+1]=hex[bytes[i]&15];}
}
static const GLenum dg_arb_source_counts[DG_ARB_SOURCE_COUNTS]={GL_PROGRAM_INSTRUCTIONS_ARB,
    GL_PROGRAM_TEMPORARIES_ARB,GL_PROGRAM_PARAMETERS_ARB,GL_PROGRAM_ATTRIBS_ARB,
    GL_PROGRAM_ALU_INSTRUCTIONS_ARB,GL_PROGRAM_TEX_INSTRUCTIONS_ARB,GL_PROGRAM_TEX_INDIRECTIONS_ARB};
static void DGHostGetARBProgramiv(struct DGHostContext *,GLenum,GLenum,GLint *);
/* Only the hardware compiler needs the additional native-resource-budget
 * check below. Float's native-limits query invalidates its IOSurface texture
 * storage on Tahoe Intel; even an unbound-FBO query faults on a later read or
 * flush (glsFlushTexture+68, texture+0x20 == NULL). Do not make that unnecessary
 * observation during software compilation. Explicit caller queries retain
 * their native semantics. This is cold compiler work, never a draw hot path. */
static int DGHostARBUsesHardware(void)
{
    CGLContextObj c=CGLGetCurrentContext();GLint screen=0,accelerated=1;
    return !c||CGLGetVirtualScreen(c,&screen)||
        CGLDescribePixelFormat(CGLGetPixelFormat(c),screen,kCGLPFAAccelerated,&accelerated)||accelerated;
}
static void DGHostARBProgramString(struct DGHostContext *c,GLenum target,GLenum format,GLsizei length,const void *source)
{
    if(c->inside_begin || target!=GL_FRAGMENT_PROGRAM_ARB || format!=GL_PROGRAM_FORMAT_ASCII_ARB || length<=0 || !DGHostARBUsesHardware()){
        glProgramStringARB(target,format,length,source);return;
    }
    GLenum prior=glGetError();if(prior && !c->error)c->error=prior;
    glProgramStringARB(target,format,length,source);
    GLenum original=glGetError();if(original){if(!c->error)c->error=original;return;}
    if((uint32_t)length>262144)return;
    int counts[DG_ARB_SOURCE_COUNTS];
    for(unsigned i=0;i<DG_ARB_SOURCE_COUNTS;++i){GLint value=0;glGetProgramivARB(target,dg_arb_source_counts[i],&value);counts[i]=(int)value;}
    GLint native_limits=0;
    glGetProgramivARB(target,GL_PROGRAM_UNDER_NATIVE_LIMITS_ARB,&native_limits);
    if(glGetError())return; // An unsupported internal observation never replaces a valid program.
    pthread_once(&dg_arb_cookie_once,DGARBInitializeCookie);
    size_t transformed_length=0;
    char *transformed=DGARBWrapSource(source,(size_t)length,dg_arb_cookie,counts,&transformed_length);
    if(!transformed)return;
    glProgramStringARB(target,format,(GLsizei)transformed_length,transformed);free(transformed);
    GLenum error=glGetError();
    GLint new_native_limits=0;
    if(!error && native_limits){glGetProgramivARB(target,GL_PROGRAM_UNDER_NATIVE_LIMITS_ARB,&new_native_limits);error=glGetError();}
    if(error || (native_limits && !new_native_limits)){
        glProgramStringARB(target,format,length,source);
        error=glGetError();if(error && !c->error)c->error=error;
    }
}
/* Reads the native CURRENT object, so bound deleted objects, name reuse,
 * context zero-programs and shared-source revisions cannot collide in a cache.
 * Returned text is the original caller bytes if this executor normalized it.
 */
static char *DGHostARBSource(GLenum target,size_t *size,int *tagged,int counts[DG_ARB_SOURCE_COUNTS],GLenum *error)
{
    *size=0;*tagged=0;*error=GL_NO_ERROR;
    GLint length=0;glGetProgramivARB(target,GL_PROGRAM_LENGTH_ARB,&length);
    *error=glGetError();if(*error)return NULL;
    if(length<0 || (uint32_t)length>DG_MAX_BYTES){*error=GL_OUT_OF_MEMORY;return NULL;}
    char *raw=malloc(length?(size_t)length:1);if(!raw){*error=GL_OUT_OF_MEMORY;return NULL;}
    glGetProgramStringARB(target,GL_PROGRAM_STRING_ARB,raw);
    *error=glGetError();if(*error){free(raw);return NULL;}
    *size=(size_t)length;
    if(target==GL_FRAGMENT_PROGRAM_ARB){
        pthread_once(&dg_arb_cookie_once,DGARBInitializeCookie);
        size_t original_length=0;
        if(DGARBUnwrapSource(raw,*size,dg_arb_cookie,counts,&original_length)){*size=original_length;*tagged=1;}
    }
    return raw;
}
/* Apple's Metal-backed legacy renderer accepts and executes native ARB
 * programs but reports every native resource count as zero. Tiger CI treats
 * zero native instructions as a compiler failure even when the actual
 * compiler reports UNDER_NATIVE_LIMITS and no error. Describe the virtual
 * ARB machine's real compiled instruction/register costs in that opaque case,
 * not invented hardware limits or an unconditional compilation-success flag.
 * Real nonzero native counts, software renderers and failed programs retain
 * their original answers. This is program introspection, never draw-time work.
 */
static GLenum DGHostARBLogicalCost(GLenum native)
{
    switch(native){
    case GL_PROGRAM_NATIVE_INSTRUCTIONS_ARB:return GL_PROGRAM_INSTRUCTIONS_ARB;
    case GL_PROGRAM_NATIVE_TEMPORARIES_ARB:return GL_PROGRAM_TEMPORARIES_ARB;
    case GL_PROGRAM_NATIVE_PARAMETERS_ARB:return GL_PROGRAM_PARAMETERS_ARB;
    case GL_PROGRAM_NATIVE_ATTRIBS_ARB:return GL_PROGRAM_ATTRIBS_ARB;
    case GL_PROGRAM_NATIVE_ADDRESS_REGISTERS_ARB:return GL_PROGRAM_ADDRESS_REGISTERS_ARB;
    case GL_PROGRAM_NATIVE_ALU_INSTRUCTIONS_ARB:return GL_PROGRAM_ALU_INSTRUCTIONS_ARB;
    case GL_PROGRAM_NATIVE_TEX_INSTRUCTIONS_ARB:return GL_PROGRAM_TEX_INSTRUCTIONS_ARB;
    case GL_PROGRAM_NATIVE_TEX_INDIRECTIONS_ARB:return GL_PROGRAM_TEX_INDIRECTIONS_ARB;
    default:return 0;
    }
}
static void DGHostARBOpaqueNativeCost(GLenum target,GLenum pname,GLint *value)
{
    GLenum logical=DGHostARBLogicalCost(pname);
    if(!logical||*value||!CGLGetCurrentContext())return;
    CGLContextObj context=CGLGetCurrentContext();GLint screen=0,accelerated=0;
    if(CGLGetVirtualScreen(context,&screen)||
       CGLDescribePixelFormat(CGLGetPixelFormat(context),screen,kCGLPFAAccelerated,&accelerated)||!accelerated)return;
    /* Do not issue the extra native-limit observation on Float: it is not
     * needed there and damages its IOSurface state on Tahoe Intel. */
    GLint native=0,under=0,instructions=0,position=-1;
    if(pname!=GL_PROGRAM_NATIVE_INSTRUCTIONS_ARB)glGetProgramivARB(target,GL_PROGRAM_NATIVE_INSTRUCTIONS_ARB,&native);
    if(glGetError()||native)return;
    glGetProgramivARB(target,GL_PROGRAM_UNDER_NATIVE_LIMITS_ARB,&under);
    glGetProgramivARB(target,GL_PROGRAM_INSTRUCTIONS_ARB,&instructions);
    glGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB,&position);
    if(glGetError()||!under||instructions<=0||position!=-1)return;
    GLint cost=0;glGetProgramivARB(target,logical,&cost);
    if(!glGetError()&&cost>=0)*value=cost;
}
static void DGHostGetARBProgramiv(struct DGHostContext *c,GLenum target,GLenum pname,GLint *params)
{
    if(c->inside_begin){glGetProgramivARB(target,pname,params);return;}
    GLenum prior=glGetError();if(prior && !c->error)c->error=prior;
    glGetProgramivARB(target,pname,params);
    GLenum error=glGetError();if(error){if(!c->error)c->error=error;return;}
    if(target==GL_FRAGMENT_PROGRAM_ARB||target==GL_VERTEX_PROGRAM_ARB)
        DGHostARBOpaqueNativeCost(target,pname,params);
    if(target!=GL_FRAGMENT_PROGRAM_ARB)return;
    int index=-1;
    for(unsigned i=0;i<DG_ARB_SOURCE_COUNTS;++i)if(pname==dg_arb_source_counts[i])index=(int)i;
    if(pname!=GL_PROGRAM_LENGTH_ARB && index<0)return;
    GLint native_length=0;glGetProgramivARB(target,GL_PROGRAM_LENGTH_ARB,&native_length);
    error=glGetError();if(error){if(!c->error)c->error=error;return;}
    if(native_length<1 || native_length>(GLint)DG_ARB_SOURCE_LIMIT)return;
    size_t length=0;int tagged=0,counts[DG_ARB_SOURCE_COUNTS];
    char *source=DGHostARBSource(target,&length,&tagged,counts,&error);
    if(error){if(!c->error)c->error=error;return;}
    if(tagged)*params=pname==GL_PROGRAM_LENGTH_ARB?(GLint)length:(GLint)counts[index];
    free(source);
}
static void DGHostUniformLocation(struct DGHostContext *c,GLuint program,const GLchar *name,struct DGReply *reply)
{
    GLint result=-1;
    if(c->inside_begin)reply->error=GL_INVALID_OPERATION;
    else {
        /* A successful transport reply is NOT enough to cache a -1: the
         * query may have failed natively (e.g. a failed program relink).
         * Preserve prior errors separately, then report this query's error
         * in its existing typed reply. Old clients understand that too. */
        GLenum prior=glGetError();if(prior && !c->error)c->error=prior;
        result=glGetUniformLocation(program,name);reply->error=glGetError();
    }
    reply->result=DGBits(&result,sizeof(result));
}
static int DGHostHasExtension(const char *extensions,const char *name)
{
    if(!extensions)return 0;size_t n=strlen(name);
    for(const char *p=extensions;(p=strstr(p,name));p+=n)
        if((p==extensions || p[-1]==' ') && (!p[n] || p[n]==' '))return 1;
    return 0;
}
static int DGProgramString(struct DGHostContext *c,struct DGMessage *m,struct DGReply *r)
{
    if(m->flags!=(DG_REPLY|DG_OUTPUT_ONLY))return -1;
    if(c->inside_begin){r->error=GL_INVALID_OPERATION;return 0;}
    GLenum target=(GLenum)m->args[0],pname=(GLenum)m->args[1];
    if(pname!=GL_PROGRAM_STRING_ARB){r->error=GL_INVALID_ENUM;return 0;}
    GLenum prior=glGetError();if(prior && !c->error)c->error=prior;
    size_t length=0;int tagged=0,counts[DG_ARB_SOURCE_COUNTS];GLenum error=GL_NO_ERROR;
    char *source=DGHostARBSource(target,&length,&tagged,counts,&error);
    r->error=error;if(error)return 0;
    if(length>m->bytes){free(source);r->error=GL_INVALID_OPERATION;return 0;}
    unsigned char empty=0;
    void *out=length?DGHostPointer(m,2,(size_t)length):&empty;
    if(!out){free(source);return -1;}
    if(length)memcpy(out,source,length);free(source);r->result=(uint32_t)length;
    return 0;
}
