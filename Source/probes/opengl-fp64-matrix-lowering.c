/* Independent finite determinant control: permutation sum, not shader cofactors. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <math.h>
#include <stdio.h>
#include "../PassthroughOpenGL/passthrough-opengl-shader-compile.h"
static long double permutations(const double *m,int n,int row,unsigned used,int *columns)
{
    if(row==n){
        long double product=1;int inversions=0;
        for(int r=0;r<n;++r){product*=m[columns[r]*4+r];
            for(int s=0;s<r;++s)inversions+=columns[s]>columns[r];}
        return inversions&1?-product:product;
    }
    long double result=0;
    for(int c=0;c<n;++c)if(!(used&(1u<<c))){columns[row]=c;result+=permutations(m,n,row+1,used|(1u<<c),columns);}
    return result;
}
int main(int argc,char **argv)
{
    int adapter=0;unsigned version=400;
    for(int i=1;i<argc;++i){
        if(!strcmp(argv[i],"--adapter"))adapter=1;
        else if(!strcmp(argv[i],"--glsl")&&i+1<argc){
            const char *value=argv[++i];
            if(!strcmp(value,"150"))version=150;
            else if(!strcmp(value,"330"))version=330;
            else if(!strcmp(value,"400"))version=400;
            else if(!strcmp(value,"410"))version=410;
            else return 64;
        }else return 64;
    }
    const CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attributes,&format,&count)||!format)return 77;
    if(CGLCreateContext(format,NULL,&context)||CGLSetCurrentContext(context))return 2;
    CGLDestroyPixelFormat(format);printf("matrix-lowering renderer=%s adapter=%d GLSL=%u\n",glGetString(GL_RENDERER),adapter,version);
    char prefix[96];snprintf(prefix,sizeof(prefix),"#version %u core\n#extension GL_ARB_gpu_shader_fp64 : enable\n",version);
    const char *sources[]={prefix,
        "uniform dmat4 input_matrix;flat out dvec4 answer;void main(){"
        "answer=dvec4(determinant(dmat2(input_matrix)),determinant(dmat3(input_matrix)),"
        "determinant(input_matrix),double(determinant(mat4(2))));gl_Position=vec4(0,0,0,1);}"};
    GLuint shader=glCreateShader(GL_VERTEX_SHADER);glShaderSource(shader,2,sources,NULL);
    if(adapter){if(DGCompileAdaptedShader(shader,0))return 2;}else glCompileShader(shader);
    GLint ok=0;char log[4096];glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok){glGetShaderInfoLog(shader,sizeof(log),NULL,log);fprintf(stderr,"compile: %s\n",log);return 1;}
    GLuint program=glCreateProgram();glAttachShader(program,shader);const char *varying="answer";
    glTransformFeedbackVaryings(program,1,&varying,GL_INTERLEAVED_ATTRIBS);glLinkProgram(program);
    glGetProgramiv(program,GL_LINK_STATUS,&ok);
    if(!ok){glGetProgramInfoLog(program,sizeof(log),NULL,log);fprintf(stderr,"link: %s\n",log);return 1;}
    glUseProgram(program);GLint uniform=glGetUniformLocation(program,"input_matrix");
    GLuint vao=0,buffer=0,texture=0,framebuffer=0;glGenVertexArrays(1,&vao);glBindVertexArray(vao);
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffers(1,&framebuffer);glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)return 2;
    glGenBuffers(1,&buffer);glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,buffer);
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,4*sizeof(double),NULL,GL_STREAM_READ);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,buffer);glEnable(GL_RASTERIZER_DISCARD);
    unsigned random=0x18273645;int failures=0;
    for(int test=0;test<66;++test){
        double matrix[16],output[4];
        for(int i=0;i<16;++i){random=random*1664525u+1013904223u;
            matrix[i]=test==0?-511.5+i*.125:test==1?(i%5==0?1:0):
                ((int)((random>>16)%65)-32)*.25*(test&1?1048576.0:1.0);}
        glUniformMatrix4dv(uniform,1,GL_FALSE,matrix);
        glBeginTransformFeedback(GL_POINTS);glDrawArrays(GL_POINTS,0,1);glEndTransformFeedback();
        glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER,0,sizeof(output),output);
        for(int i=0;i<4;++i){int columns[4];
            long double expected=i<3?permutations(matrix,i+2,0,0,columns):16;
            if(fabsl((long double)output[i]-expected)>1e-11L*fmaxl(1,fabsl(expected))){
                fprintf(stderr,"FAIL matrix=%d dimension=%d result=%.17g expected=%.21Lg\n",test,i+2,output[i],expected);++failures;}
        }
    }
    GLenum error=glGetError();if(error){fprintf(stderr,"GL error=%x\n",error);++failures;}
    glDeleteBuffers(1,&buffer);glDeleteFramebuffers(1,&framebuffer);glDeleteTextures(1,&texture);
    glDeleteVertexArrays(1,&vao);glDeleteProgram(program);glDeleteShader(shader);
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);
    printf("matrix-lowering results=264 failures=%d\n",failures);return failures?1:0;
}
