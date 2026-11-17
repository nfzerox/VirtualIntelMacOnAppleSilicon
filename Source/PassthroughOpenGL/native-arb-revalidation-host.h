/* A CGL software compiler visit can leave ARB programs executing in Float
 * after CGLSetVirtualScreen returns to hardware. Re-submit the currently
 * bound native objects at that cold transition: no GLuint lifetime cache,
 * shader translation, per-draw query or changed application source.
 */
#ifndef VIM_NATIVE_ARB_REVALIDATION_HOST_H
#define VIM_NATIVE_ARB_REVALIDATION_HOST_H
#include <stdlib.h>
static GLenum DGRevalidateNativeARBPrograms(void)
{
    /* Do not erase the application's last failed-compile diagnostics. */
    GLint position=-1;glGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB,&position);
    GLenum error=glGetError();if(error)return error;if(position!=-1)return GL_NO_ERROR;
    const GLenum targets[]={GL_VERTEX_PROGRAM_ARB,GL_FRAGMENT_PROGRAM_ARB};
    for(unsigned i=0;i<sizeof(targets)/sizeof(*targets);++i){
        GLint length=0;glGetProgramivARB(targets[i],GL_PROGRAM_LENGTH_ARB,&length);
        error=glGetError();if(error)return error;
        if(length<=0)continue;
        char *source=malloc((size_t)length);if(!source)return GL_OUT_OF_MEMORY;
        glGetProgramStringARB(targets[i],GL_PROGRAM_STRING_ARB,source);
        error=glGetError();
        if(!error){
            glProgramStringARB(targets[i],GL_PROGRAM_FORMAT_ASCII_ARB,length,source);
            error=glGetError();
        }
        free(source);if(error)return error;
    }
    return GL_NO_ERROR;
}
#endif
