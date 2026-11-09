/* Bounded native/bridge comparison for the CTS globals/sampling subroutine
 * case. Inject only into an explicitly launched diagnostic process. No
 * installation, error consumption, or production logging. Optional native
 * controls compare the ARB sampler entry point and the bridge's pixel format;
 * leave them unset for an observational trace.
 */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
extern void glUniform1iARB(GLint,GLint);

static CGLContextObj traced;
static unsigned lines;
static int active(void)
{return traced && CGLGetCurrentContext()==traced && lines++<256;}
#define LOG(...) do {if(active())fprintf(stderr,"SUBTRACE " __VA_ARGS__);}while(0)
static int contains(const char *s,size_t n,const char *needle)
{
    size_t m=strlen(needle);
    for(size_t i=0;i+m<=n;++i)if(!memcmp(s+i,needle,m))return 1;
    return 0;
}
static void source(GLuint shader,GLsizei count,const GLchar *const *strings,const GLint *lengths)
{
    for(GLsizei i=0;i<count;++i)if(strings[i]){
        size_t n=lengths && lengths[i]>=0?(size_t)lengths[i]:strlen(strings[i]);
        if(contains(strings[i],n,"discard_fragment_type")){
            traced=CGLGetCurrentContext();lines=0;
            fprintf(stderr,"SUBTRACE armed shader=%u\n",shader);
        }
    }
    glShaderSource(shader,count,strings,lengths);
}
static void use(GLuint program)
{LOG("use %u\n",program);glUseProgram(program);}
static void link_program(GLuint program)
{LOG("link %u\n",program);glLinkProgram(program);}
static GLint uniform_location(GLuint program,const GLchar *name)
{
    GLint value=glGetUniformLocation(program,name);
    LOG("uniform %u %s = %d\n",program,name?name:"(null)",value);
    return value;
}
static GLint subroutine_location(GLuint program,GLenum stage,const GLchar *name)
{
    GLint value=glGetSubroutineUniformLocation(program,stage,name);
    LOG("subuniform %u %x %s = %d\n",program,stage,name?name:"(null)",value);
    return value;
}
static GLuint subroutine_index(GLuint program,GLenum stage,const GLchar *name)
{
    GLuint value=glGetSubroutineIndex(program,stage,name);
    LOG("subindex %u %x %s = %u\n",program,stage,name?name:"(null)",value);
    return value;
}
static void uniform_integer(GLint location,GLint value)
{
    LOG("uniform1i %d %d\n",location,value);
    if(getenv("VIM_TRACE_USE_ARB_UNIFORMS"))glUniform1iARB(location,value);
    else glUniform1i(location,value);
}
static void uniform_integer_arb(GLint location,GLint value)
{LOG("uniform1iARB %d %d\n",location,value);glUniform1iARB(location,value);}
static void subroutine_values(GLenum stage,GLsizei count,const GLuint *values)
{
    if(active()){
        fprintf(stderr,"SUBTRACE values stage=%x count=%d",stage,count);
        if(values)for(GLsizei i=0;i<count && i<32;++i)fprintf(stderr," %u",values[i]);
        fputc('\n',stderr);
    }
    glUniformSubroutinesuiv(stage,count,values);
}
static void draw(GLenum mode,GLint first,GLsizei count)
{LOG("draw %x %d %d\n",mode,first,count);glDrawArrays(mode,first,count);LOG("draw returned\n");}
static void stage_query(GLuint program,GLenum stage,GLenum name,GLint *value)
{
    glGetProgramStageiv(program,stage,name,value);
    LOG("stage %u %x %x = %d\n",program,stage,name,value?*value:-1);
}
static void destroy(GLuint program)
{LOG("delete %u\n",program);glDeleteProgram(program);}
static CGLError choose(const CGLPixelFormatAttribute *attributes,CGLPixelFormatObj *format,GLint *count)
{
    if(getenv("VIM_TRACE_MATCH_BRIDGE_FORMAT")){
        const CGLPixelFormatAttribute bridge[]={kCGLPFAAccelerated,kCGLPFAAllowOfflineRenderers,
            kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core,
            kCGLPFADepthSize,24,kCGLPFAStencilSize,8,0};
        return CGLChoosePixelFormat(bridge,format,count);
    }
    return CGLChoosePixelFormat(attributes,format,count);
}
#define INTERPOSE(replacement,original) \
    __attribute__((used,section("__DATA,__interpose"))) \
    static const struct {const void *new_function,*old_function;} hook_##replacement = \
        {(const void *)(uintptr_t)&replacement,(const void *)(uintptr_t)&original};
INTERPOSE(source,glShaderSource)
INTERPOSE(use,glUseProgram)
INTERPOSE(link_program,glLinkProgram)
INTERPOSE(uniform_location,glGetUniformLocation)
INTERPOSE(subroutine_location,glGetSubroutineUniformLocation)
INTERPOSE(subroutine_index,glGetSubroutineIndex)
INTERPOSE(uniform_integer,glUniform1i)
INTERPOSE(uniform_integer_arb,glUniform1iARB)
INTERPOSE(subroutine_values,glUniformSubroutinesuiv)
INTERPOSE(draw,glDrawArrays)
INTERPOSE(stage_query,glGetProgramStageiv)
INTERPOSE(destroy,glDeleteProgram)
INTERPOSE(choose,CGLChoosePixelFormat)
