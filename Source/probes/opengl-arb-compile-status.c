/* Compile exact captured ARB source through public native CGL. No rendering,
 * source rewriting, capability override, system installation or injection. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv)
{
    if(argc!=2)return 64;
    FILE *file=fopen(argv[1],"rb");if(!file)return 1;
    if(fseek(file,0,SEEK_END)){fclose(file);return 1;}
    long length=ftell(file);
    if(length<10||length>262144||fseek(file,0,SEEK_SET)){fclose(file);return 1;}
    char *source=malloc((size_t)length+1);
    if(!source||fread(source,1,(size_t)length,file)!=(size_t)length){free(source);fclose(file);return 1;}
    fclose(file);source[length]=0;
    GLenum target=!strncmp(source,"!!ARBvp1.0",10)?GL_VERTEX_PROGRAM_ARB:
        !strncmp(source,"!!ARBfp1.0",10)?GL_FRAGMENT_PROGRAM_ARB:0;
    if(!target){free(source);return 64;}
    CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
    CGLError error=CGLChoosePixelFormat(attributes,&format,&count);
    if(!error&&format)error=CGLCreateContext(format,NULL,&context);
    if(format)CGLDestroyPixelFormat(format);
    if(error||!context){free(source);return 2;}
    error=CGLSetCurrentContext(context);
    if(error){CGLDestroyContext(context);free(source);return 2;}
    printf("renderer=%s version=%s target=%x bytes=%ld\n",glGetString(GL_RENDERER),
        glGetString(GL_VERSION),(unsigned)target,length);
    GLuint name=0;glGenProgramsARB(1,&name);glBindProgramARB(target,name);
    glProgramStringARB(target,GL_PROGRAM_FORMAT_ASCII_ARB,(GLsizei)length,source);
    GLenum compile=glGetError();GLint position=-2;
    glGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB,&position);
    const char *diagnostic=(const char *)glGetString(GL_PROGRAM_ERROR_STRING_ARB);
    printf("compile_error=%x position=%d diagnostic=%s\n",(unsigned)compile,(int)position,
        diagnostic?diagnostic:"<none>");
    const GLenum keys[]={GL_PROGRAM_UNDER_NATIVE_LIMITS_ARB,
        GL_PROGRAM_INSTRUCTIONS_ARB,GL_PROGRAM_NATIVE_INSTRUCTIONS_ARB,
        GL_MAX_PROGRAM_INSTRUCTIONS_ARB,GL_MAX_PROGRAM_NATIVE_INSTRUCTIONS_ARB,
        GL_PROGRAM_TEMPORARIES_ARB,GL_PROGRAM_NATIVE_TEMPORARIES_ARB,
        GL_MAX_PROGRAM_TEMPORARIES_ARB,GL_MAX_PROGRAM_NATIVE_TEMPORARIES_ARB,
        GL_PROGRAM_PARAMETERS_ARB,GL_PROGRAM_NATIVE_PARAMETERS_ARB,
        GL_MAX_PROGRAM_PARAMETERS_ARB,GL_MAX_PROGRAM_NATIVE_PARAMETERS_ARB};
    for(unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);++i){
        GLint value=-2;glGetProgramivARB(target,keys[i],&value);
        printf("query=%x value=%d error=%x\n",(unsigned)keys[i],(int)value,(unsigned)glGetError());
    }
    if(target==GL_FRAGMENT_PROGRAM_ARB){
        const GLenum fragment[]={GL_PROGRAM_ALU_INSTRUCTIONS_ARB,GL_PROGRAM_TEX_INSTRUCTIONS_ARB,
            GL_PROGRAM_TEX_INDIRECTIONS_ARB,GL_PROGRAM_NATIVE_ALU_INSTRUCTIONS_ARB,
            GL_PROGRAM_NATIVE_TEX_INSTRUCTIONS_ARB,GL_PROGRAM_NATIVE_TEX_INDIRECTIONS_ARB,
            GL_MAX_PROGRAM_NATIVE_ALU_INSTRUCTIONS_ARB,GL_MAX_PROGRAM_NATIVE_TEX_INSTRUCTIONS_ARB,
            GL_MAX_PROGRAM_NATIVE_TEX_INDIRECTIONS_ARB};
        for(unsigned i=0;i<sizeof(fragment)/sizeof(fragment[0]);++i){
            GLint value=-2;glGetProgramivARB(target,fragment[i],&value);
            printf("query=%x value=%d error=%x\n",(unsigned)fragment[i],(int)value,(unsigned)glGetError());
        }
    }
    glDeleteProgramsARB(1,&name);CGLSetCurrentContext(NULL);CGLDestroyContext(context);free(source);
    return compile?3:0;
}
