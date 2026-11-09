/* Public transform-feedback control for FP64 rounding and source reflection.
 * No prelude/--adapter means the unmodified public GL path. GLSL150/330
 * exercise the extension spelling, separately from core GLSL400/410. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fenv.h>
#include "../PassthroughOpenGL/passthrough-opengl-shader-compile.h"

int main(int argc,char **argv)
{
    const char *prelude_path=NULL;int restore_source=0,adapter=0;unsigned version=400;
    for(int i=1;i<argc;++i){
        if(!strcmp(argv[i],"--restore-source"))restore_source=1;
        else if(!strcmp(argv[i],"--adapter"))adapter=1;
        else if(!strcmp(argv[i],"--glsl")&&i+1<argc){
            const char *value=argv[++i];
            if(!strcmp(value,"150"))version=150;
            else if(!strcmp(value,"330"))version=330;
            else if(!strcmp(value,"400"))version=400;
            else if(!strcmp(value,"410"))version=410;
            else return 64;
        }else if(argv[i][0]!='-'&&!prelude_path)prelude_path=argv[i];
        else return 64;
    }
    const CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attributes,&format,&count)||!format)return 77;
    if(CGLCreateContext(format,NULL,&context)||CGLSetCurrentContext(context))return 2;
    CGLDestroyPixelFormat(format);fesetround(FE_TONEAREST);
    printf("guest-lowering renderer=%s prelude=%d adapter=%d GLSL=%u\n",glGetString(GL_RENDERER),prelude_path!=NULL,adapter,version);
    char *prelude=calloc(1,65536);if(!prelude)return 2;
    if(prelude_path){
        FILE *file=fopen(prelude_path,"rb");if(!file){perror(prelude_path);return 2;}
        size_t length=fread(prelude,1,65535,file);int extra=fgetc(file);
        if(ferror(file)||extra!=EOF){fclose(file);return 2;}fclose(file);prelude[length]=0;
    }
    char prefix[96];snprintf(prefix,sizeof(prefix),"#version %u core\n#extension GL_ARB_gpu_shader_fp64 : enable\n",version);
    const GLchar *sources[]={prefix,prelude,
        "\nuniform dvec4 input_value;flat out dvec4 answer;"
        "void main(){answer=roundEven(input_value);gl_Position=vec4(0,0,0,1);}"};
    GLuint shader=glCreateShader(GL_VERTEX_SHADER);glShaderSource(shader,3,sources,NULL);
    if(adapter){if(DGCompileAdaptedShader(shader,0))return 2;}else glCompileShader(shader);
    GLint ok=0;char log[8192]={0};glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok){glGetShaderInfoLog(shader,sizeof(log),NULL,log);fprintf(stderr,"compile: %s\n",log);return 1;}
    if(restore_source){
        char compile_log[8192]={0},restored_log[8192]={0},original[8192]={0},queried[8192]={0};
        glGetShaderInfoLog(shader,sizeof(compile_log),NULL,compile_log);
        const GLchar *unmodified[]={sources[0],sources[2]};
        GLuint baseline=glCreateShader(GL_VERTEX_SHADER);GLsizei baseline_actual=0;GLint baseline_length=0;
        glShaderSource(baseline,2,unmodified,NULL);
        glGetShaderSource(baseline,sizeof(original),&baseline_actual,original);
        glGetShaderiv(baseline,GL_SHADER_SOURCE_LENGTH,&baseline_length);glDeleteShader(baseline);
        const GLchar *restore=original;GLint restore_length=baseline_actual;
        glShaderSource(shader,1,&restore,&restore_length);
        GLint source_length=0;GLsizei actual=0;
        glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
        glGetShaderiv(shader,GL_SHADER_SOURCE_LENGTH,&source_length);
        glGetShaderSource(shader,sizeof(queried),&actual,queried);
        glGetShaderInfoLog(shader,sizeof(restored_log),NULL,restored_log);
        GLenum restore_error=glGetError();
        if(!ok||source_length!=baseline_length||actual!=baseline_actual||
           strcmp(original,queried)||strcmp(compile_log,restored_log)||restore_error!=GL_NO_ERROR){
            fprintf(stderr,"FAIL restoring source: compiled=%d length=%d actual=%d expected=%lu source-equal=%d log-equal=%d error=%x\n",
                ok,source_length,actual,(unsigned long)baseline_actual,!strcmp(original,queried),!strcmp(compile_log,restored_log),restore_error);
            fprintf(stderr,"before-log: %s\nafter-log: %s\n",compile_log,restored_log);return 1;
        }
        puts("PASS original source, length, compile status and log survive restoration before linking");
    }
    GLuint program=glCreateProgram();glAttachShader(program,shader);
    const char *varying="answer";glTransformFeedbackVaryings(program,1,&varying,GL_INTERLEAVED_ATTRIBS);
    glLinkProgram(program);glGetProgramiv(program,GL_LINK_STATUS,&ok);
    if(!ok){glGetProgramInfoLog(program,sizeof(log),NULL,log);fprintf(stderr,"link: %s\n",log);return 1;}
    glUseProgram(program);GLint uniform=glGetUniformLocation(program,"input_value");
    GLuint vao=0,buffer=0,framebuffer=0,texture=0;glGenVertexArrays(1,&vao);glBindVertexArray(vao);
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffers(1,&framebuffer);glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)return 2;
    glViewport(0,0,4,4);
    glGenBuffers(1,&buffer);glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,buffer);
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,4*sizeof(double),NULL,GL_STREAM_READ);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,buffer);glEnable(GL_RASTERIZER_DISCARD);
    const double values[]={-511.5,-510.5,-422.5,-110.5,-2.5,-1.5,-0.5,-0.0,
        0.0,0.5,1.5,2.5,510.5,511.5,2147483647.5,2147483648.5,
        -2147483647.5,-2147483648.5,4503599627370495.5,-4503599627370495.5,
        INFINITY,-INFINITY,NAN,1.7976931348623157e308};
    int failures=0;
    for(unsigned test=0;test<sizeof(values)/sizeof(*values);++test){
        double input[4],output[4]={0};
        for(unsigned i=0;i<4;++i)input[i]=values[(test+i)%(sizeof(values)/sizeof(*values))];
        glUniform4dv(uniform,1,input);glBeginTransformFeedback(GL_POINTS);glDrawArrays(GL_POINTS,0,1);
        glEndTransformFeedback();glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER,0,sizeof(output),output);
        for(unsigned i=0;i<4;++i){double expected=nearbyint(input[i]);
            if(output[i]!=expected&&!(isnan(output[i])&&isnan(expected))){
                fprintf(stderr,"FAIL input=%.17g result=%.17g expected=%.17g\n",input[i],output[i],expected);++failures;
            }
        }
    }
    GLenum error=glGetError();if(error){fprintf(stderr,"GL error=%x\n",error);++failures;}
    glDeleteBuffers(1,&buffer);glDeleteVertexArrays(1,&vao);glDeleteProgram(program);glDeleteShader(shader);
    glDeleteFramebuffers(1,&framebuffer);glDeleteTextures(1,&texture);
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);free(prelude);
    printf("guest-lowering failures=%d\n",failures);return failures?1:0;
}
