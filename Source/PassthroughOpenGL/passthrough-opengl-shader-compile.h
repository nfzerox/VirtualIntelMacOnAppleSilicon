/* Ordinary public GL calls in our executor. No Apple image/import hooks.
 * Keep GL's native shader-object lifetime and source/reflection ownership:
 * adapt only compilation, then restore the source without recompiling it. */
#ifndef VIM_PASSTHROUGH_OPENGL_SHADER_COMPILE_H
#define VIM_PASSTHROUGH_OPENGL_SHADER_COMPILE_H
#if !defined(__gl_h_)&&!defined(__gl3_h_)
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#endif
#include <limits.h>
#include "passthrough-opengl-shader-lowering.h"
#ifndef DG_SHADER_LOWERED
#define DG_SHADER_LOWERED() ((void)0)
#endif
#ifndef DG_SHADER_LOWER_FAILED
#define DG_SHADER_LOWER_FAILED(shader) ((void)0)
#endif
static int DGNativeMetalShaderBackend(void)
{const char *version=(const char *)glGetString(GL_VERSION);return version&&strstr(version,"Metal");}
static GLenum DGCompileAdaptedShader(GLuint shader,int inside_begin)
{
    if(inside_begin||!glIsShader(shader)){glCompileShader(shader);return GL_NO_ERROR;}
    /* This is a public backend classification, not a CPU-slice or OS-build
     * assumption. Native Intel/AMD/NVIDIA implementations retain their path. */
    if(!DGNativeMetalShaderBackend()){glCompileShader(shader);return GL_NO_ERROR;}
    GLint capacity=0;glGetShaderiv(shader,GL_SHADER_SOURCE_LENGTH,&capacity);
    if(capacity<=1){glCompileShader(shader);return GL_NO_ERROR;}
    char *source=malloc((size_t)capacity);if(!source)return GL_OUT_OF_MEMORY;
    GLsizei actual=0;glGetShaderSource(shader,capacity,&actual,source);
    if(actual<0||actual>=capacity){free(source);glCompileShader(shader);return GL_NO_ERROR;}
    char *lowered=NULL;size_t length=0;
    int changed=DGLowerShaderSource(source,(size_t)actual,&lowered,&length);
    if(changed<0||length>INT_MAX){free(lowered);free(source);return GL_OUT_OF_MEMORY;}
    if(!changed){free(source);glCompileShader(shader);return GL_NO_ERROR;}
    const GLchar *temporary=lowered;GLint temporary_length=(GLint)length;
    glShaderSource(shader,1,&temporary,&temporary_length);glCompileShader(shader);
    GLint compiled=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&compiled);
    if(!compiled)DG_SHADER_LOWER_FAILED(shader);
    const GLchar *original=source;GLint original_length=actual;
    glShaderSource(shader,1,&original,&original_length);
    if(compiled){DG_SHADER_LOWERED();}
    else {
        /* Preserve the native application's diagnostics/behavior for source
         * outside the adapter's grammar. Never drain its GL error queue. */
        glCompileShader(shader);
    }
    free(lowered);free(source);return GL_NO_ERROR;
}
typedef GLuint (*DGNativeCreateShaderProgram)(GLenum,GLsizei,const GLchar *const *);
static GLuint DGCreateAdaptedShaderProgram(DGNativeCreateShaderProgram create,GLenum type,
    GLsizei count,const GLchar *const *strings,GLenum *error)
{
    *error=GL_NO_ERROR;
    if(count<=0||!strings||!DGNativeMetalShaderBackend())return create(type,count,strings);
    size_t total=0;
    for(GLsizei i=0;i<count;++i){
        if(!strings[i])return create(type,count,strings);
        size_t n=strlen(strings[i]);
        if(n>SIZE_MAX-total-1){*error=GL_OUT_OF_MEMORY;return 0;}total+=n;
    }
    char *source=malloc(total+1);if(!source){*error=GL_OUT_OF_MEMORY;return 0;}
    size_t offset=0;
    for(GLsizei i=0;i<count;++i){size_t n=strlen(strings[i]);memcpy(source+offset,strings[i],n);offset+=n;}
    source[total]=0;char *lowered=NULL;size_t length=0;
    int changed=DGLowerShaderSource(source,total,&lowered,&length);free(source);
    if(changed<0){*error=GL_OUT_OF_MEMORY;return 0;}
    if(!changed)return create(type,count,strings);
    const GLchar *temporary=lowered;GLuint program=create(type,1,&temporary);free(lowered);
    if(program){
        GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);
        if(linked){DG_SHADER_LOWERED();}
        else {glDeleteProgram(program);program=create(type,count,strings);}
    }
    return program;
}
#endif
