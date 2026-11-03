/* Actual production ARB compile/query helpers against native GL contexts.
 * Finite owned objects only; no VM, system files, injection or preferences. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <pthread.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "../PassthroughOpenGL/passthrough-opengl-wire.h"
struct DGHostContext {unsigned inside_begin;GLenum error;};
static void *DGHostPointer(struct DGMessage *m,int index,size_t size)
{
    uint64_t p=m->args[index];
    return p && p-1<=m->bytes && size<=m->bytes-(p-1)?(char *)(m+1)+p-1:NULL;
}
#include "../PassthroughOpenGL/passthrough-opengl-program-host.c"
static const char first[]="!!ARBfp1.0\nTEMP r;\nMOV r.x,1.0;\nMOV result.color,r;\nEND\n";
static const char second[]="!!ARBfp1.0\nTEMP a,b;\nMOV a.y,1.0;\nMOV b,a;\nMOV result.color,b;\nEND\n";
static const char plain[]="!!ARBfp1.0\nMOV result.color,1.0;\nEND\n";
static void check_source(struct DGHostContext *c,const char *expected)
{
    GLint length=-1;DGHostGetARBProgramiv(c,GL_FRAGMENT_PROGRAM_ARB,GL_PROGRAM_LENGTH_ARB,&length);
    assert(!c->error && length==(GLint)strlen(expected));
    struct {struct DGMessage m;unsigned char bytes[4096];} request;
    memset(&request,0,sizeof(request));memset(request.bytes,0xa5,sizeof(request.bytes));
    request.m.flags=DG_REPLY|DG_OUTPUT_ONLY;request.m.bytes=(uint32_t)length;
    request.m.args[0]=GL_FRAGMENT_PROGRAM_ARB;request.m.args[1]=GL_PROGRAM_STRING_ARB;request.m.args[2]=1;
    struct DGReply reply={0};assert(!DGProgramString(c,&request.m,&reply));
    assert(!reply.error && reply.result==(uint32_t)length);
    assert(!memcmp(request.bytes,expected,(size_t)length));assert(request.bytes[length]==0xa5);
}
static void upload(struct DGHostContext *c,const char *source)
{
    DGHostARBProgramString(c,GL_FRAGMENT_PROGRAM_ARB,GL_PROGRAM_FORMAT_ASCII_ARB,(GLsizei)strlen(source),source);
    assert(!c->error && !glGetError());check_source(c,source);
}
static void check_native_costs(struct DGHostContext *c)
{
    const GLenum pairs[][2]={
        {GL_PROGRAM_NATIVE_INSTRUCTIONS_ARB,GL_PROGRAM_INSTRUCTIONS_ARB},
        {GL_PROGRAM_NATIVE_TEMPORARIES_ARB,GL_PROGRAM_TEMPORARIES_ARB},
        {GL_PROGRAM_NATIVE_PARAMETERS_ARB,GL_PROGRAM_PARAMETERS_ARB},
        {GL_PROGRAM_NATIVE_ATTRIBS_ARB,GL_PROGRAM_ATTRIBS_ARB},
        {GL_PROGRAM_NATIVE_ALU_INSTRUCTIONS_ARB,GL_PROGRAM_ALU_INSTRUCTIONS_ARB},
        {GL_PROGRAM_NATIVE_TEX_INSTRUCTIONS_ARB,GL_PROGRAM_TEX_INSTRUCTIONS_ARB},
        {GL_PROGRAM_NATIVE_TEX_INDIRECTIONS_ARB,GL_PROGRAM_TEX_INDIRECTIONS_ARB}};
    GLint native_instructions=0;glGetProgramivARB(GL_FRAGMENT_PROGRAM_ARB,GL_PROGRAM_NATIVE_INSTRUCTIONS_ARB,&native_instructions);
    for(unsigned i=0;i<sizeof(pairs)/sizeof(pairs[0]);++i){
        GLint raw=-1,compiled=-1,described=-1;
        glGetProgramivARB(GL_FRAGMENT_PROGRAM_ARB,pairs[i][0],&raw);
        glGetProgramivARB(GL_FRAGMENT_PROGRAM_ARB,pairs[i][1],&compiled);
        assert(!glGetError());
        DGHostGetARBProgramiv(c,GL_FRAGMENT_PROGRAM_ARB,pairs[i][0],&described);
        assert(!c->error&&!glGetError());
        assert(described==(raw||native_instructions?raw:compiled));
        printf("native-cost key=%x raw=%d compiled=%d virtual=%d\n",pairs[i][0],raw,compiled,described);
    }
}
int main(int argc,char **argv)
{
    alarm(20);CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj format=NULL;CGLContextObj a=NULL,b=NULL;GLint count=0;
    assert(!CGLChoosePixelFormat(attrs,&format,&count) && format);
    assert(!CGLCreateContext(format,NULL,&a) && !CGLCreateContext(format,a,&b));
    assert(!CGLSetCurrentContext(a));fprintf(stderr,"renderer=%s\n",glGetString(GL_RENDERER));
    struct DGHostContext ca={0},cb={0};GLuint name=0;glGenProgramsARB(1,&name);
    glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB,name);upload(&ca,first);
    check_native_costs(&ca);
    GLint native_length=0,logical_count=0;glGetProgramivARB(GL_FRAGMENT_PROGRAM_ARB,GL_PROGRAM_LENGTH_ARB,&native_length);
    assert(native_length>(GLint)strlen(first));
    DGHostGetARBProgramiv(&ca,GL_FRAGMENT_PROGRAM_ARB,GL_PROGRAM_INSTRUCTIONS_ARB,&logical_count);assert(logical_count==2);
    assert(!CGLSetCurrentContext(b));glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB,name);check_source(&cb,first);
    assert(!CGLSetCurrentContext(a));glDeleteProgramsARB(1,&name);
    // A deleted name can immediately identify a NEW object while the original
    // remains bound in another context. No GLuint-only cache can model this.
    glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB,name);upload(&ca,second);
    assert(!CGLSetCurrentContext(b));check_source(&cb,first);
    glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB,name);check_source(&cb,second);
    // Object zero is per-context, not part of the shared nonzero namespace.
    glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB,0);upload(&cb,first);
    assert(!CGLSetCurrentContext(a));glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB,0);upload(&ca,second);
    assert(!CGLSetCurrentContext(b));check_source(&cb,first);
    upload(&cb,plain);glGetProgramivARB(GL_FRAGMENT_PROGRAM_ARB,GL_PROGRAM_LENGTH_ARB,&native_length);
    assert(native_length==(GLint)strlen(plain));
    const char invalid[]="!!ARBfp1.0\nTHIS_IS_INVALID;\nEND\n";
    glProgramStringARB(GL_FRAGMENT_PROGRAM_ARB,GL_PROGRAM_FORMAT_ASCII_ARB,sizeof(invalid)-1,invalid);
    assert(glGetError()==GL_INVALID_OPERATION);GLint baseline_position=-1;glGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB,&baseline_position);
    char bad_source[4096]={0};GLint bad_length=0;
    glGetProgramivARB(GL_FRAGMENT_PROGRAM_ARB,GL_PROGRAM_LENGTH_ARB,&bad_length);
    assert(bad_length>=0 && bad_length<(GLint)sizeof(bad_source));
    glGetProgramStringARB(GL_FRAGMENT_PROGRAM_ARB,GL_PROGRAM_STRING_ARB,bad_source);assert(!glGetError());
    upload(&cb,first);
    DGHostARBProgramString(&cb,GL_FRAGMENT_PROGRAM_ARB,GL_PROGRAM_FORMAT_ASCII_ARB,sizeof(invalid)-1,invalid);
    assert(cb.error==GL_INVALID_OPERATION);
    GLint raw_failed=0,virtual_failed=-1;
    glGetProgramivARB(GL_FRAGMENT_PROGRAM_ARB,GL_PROGRAM_NATIVE_INSTRUCTIONS_ARB,&raw_failed);
    assert(!glGetError());DGHostGetARBProgramiv(&cb,GL_FRAGMENT_PROGRAM_ARB,GL_PROGRAM_NATIVE_INSTRUCTIONS_ARB,&virtual_failed);
    assert(cb.error==GL_INVALID_OPERATION&&virtual_failed==raw_failed);cb.error=0;
    GLint position=-1;glGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB,&position);assert(position==baseline_position);
    check_source(&cb,bad_source);
    // Native error state decides admission, before any normalizer work.
    glBegin(GL_POINTS);glProgramStringARB(GL_FRAGMENT_PROGRAM_ARB,GL_PROGRAM_FORMAT_ASCII_ARB,sizeof(first)-1,first);
    glEnd();GLenum native_begin_error=glGetError();
    cb.inside_begin=1;glBegin(GL_POINTS);
    DGHostARBProgramString(&cb,GL_FRAGMENT_PROGRAM_ARB,GL_PROGRAM_FORMAT_ASCII_ARB,sizeof(first)-1,first);
    glEnd();cb.inside_begin=0;assert(glGetError()==native_begin_error);
    for(int i=1;i<argc;++i){
        FILE *file=fopen(argv[i],"rb");assert(file&&!fseek(file,0,SEEK_END));
        long bytes=ftell(file);assert(bytes>0&&bytes<=262144&&!fseek(file,0,SEEK_SET));
        char *text=malloc((size_t)bytes+1);assert(text&&fread(text,1,(size_t)bytes,file)==(size_t)bytes);
        fclose(file);text[bytes]=0;
        printf("captured-source=%s bytes=%ld\n",argv[i],bytes);
        upload(&cb,text);check_native_costs(&cb);free(text);
    }
    CGLSetCurrentContext(NULL);CGLDestroyContext(b);CGLDestroyContext(a);CGLDestroyPixelFormat(format);
    puts("ARB production helpers: source/length/counts/shared delete-reuse/default objects/compile errors: PASS");return 0;
}
