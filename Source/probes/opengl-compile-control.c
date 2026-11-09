/* Explicit-process control for the same compile adapter used by our executor.
 * Never inject this library into unrelated applications or a launchd session. */
#include <OpenGL/gl3.h>
#include <stdio.h>
#include <stdint.h>
#include <limits.h>
static uint64_t dg_compile_control_lowered_count;
#define DG_SHADER_LOWERED() __atomic_fetch_add(&dg_compile_control_lowered_count,1,__ATOMIC_RELAXED)
#define DG_SHADER_LOWER_FAILED(shader) do {char text[4096];glGetShaderInfoLog(shader,sizeof(text),NULL,text);fprintf(stderr,"ADAPTER compile: %s\n",text);}while(0)
#include "../PassthroughOpenGL/passthrough-opengl-shader-compile.h"
static void compile_shader(GLuint shader)
{
    GLenum error=DGCompileAdaptedShader(shader,0);
    if(error)fprintf(stderr,"COMPILE CONTROL adapter error=%x\n",error);
}
static GLuint create_program(GLenum type,GLsizei count,const GLchar *const *strings)
{
    GLenum error=GL_NO_ERROR;
    GLuint program=DGCreateAdaptedShaderProgram(glCreateShaderProgramv,type,count,strings,&error);
    if(error)fprintf(stderr,"COMPILE CONTROL create adapter error=%x\n",error);
    return program;
}
__attribute__((used,section("__DATA,__interpose")))
static const struct {const void *replacement,*original;} control[]={
    {(const void *)(uintptr_t)&compile_shader,(const void *)(uintptr_t)&glCompileShader},
    {(const void *)(uintptr_t)&create_program,(const void *)(uintptr_t)&glCreateShaderProgramv}};
__attribute__((destructor)) static void report_control(void)
{fprintf(stderr,"COMPILE CONTROL lowered=%llu\n",(unsigned long long)dg_compile_control_lowered_count);}
