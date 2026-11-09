/* Measure actual program-binary support separately from shader binaries.
 * A desktop GL export or a nonzero GLSL compiler flag is not sufficient. */
#ifdef VIM_PROGRAM_BINARY_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#define glProgramParameteri glProgramParameteriEXT
#define glGetUniformLocation(p,n) glGetUniformLocationARB((GLhandleARB)(uintptr_t)(p),n)
#define glUniform4f glUniform4fARB
#define glGenVertexArrays glGenVertexArraysAPPLE
#define glBindVertexArray glBindVertexArrayAPPLE
#define glGenFramebuffers glGenFramebuffersEXT
#define glBindFramebuffer glBindFramebufferEXT
#define glCheckFramebufferStatus glCheckFramebufferStatusEXT
#define glFramebufferTexture2D glFramebufferTexture2DEXT
#define GL_NUM_PROGRAM_BINARY_FORMATS 0x87fe
#define GL_PROGRAM_BINARY_FORMATS 0x87ff
#define GL_PROGRAM_BINARY_RETRIEVABLE_HINT 0x8257
#define GL_PROGRAM_BINARY_LENGTH 0x8741
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;} } while(0)

static GLuint shader(GLenum stage,const char *text)
{
    GLuint s=glCreateShader(stage);GLint compiled=0;
    glShaderSource(s,1,&text,NULL);glCompileShader(s);glGetShaderiv(s,GL_COMPILE_STATUS,&compiled);
    CHECK(compiled);return s;
}
int main(void)
{
#ifdef VIM_PROGRAM_BINARY_WIRE
    const int attrs[]={0x56494d02,DG_CGL_PROFILE_GL4,0};unsigned char screen[16*16*4]={0};
    struct DGContext *c=DGCreateContext(attrs,NULL);
    if(!c || !DGMakeCurrent(c,screen,GL_UNSIGNED_BYTE,16,16))return 2;
    CHECK(c->connection->features2&DG_FEATURE2_PROGRAM_BINARY);
#else
    const CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;
    CGLDestroyPixelFormat(pf);
#endif
    printf("program binary renderer=%s\n",glGetString(GL_RENDERER));
    GLint count[2]={-1,37};
    glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS,count);
    CHECK(glGetError()==GL_NO_ERROR && count[0]>=0 && count[0]<=256 && count[1]==37);
    if(count[0]<0 || count[0]>256)return 1;
    GLint formats[257]={0};formats[count[0]]=37;
    glGetIntegerv(GL_PROGRAM_BINARY_FORMATS,formats);
    CHECK(glGetError()==GL_NO_ERROR && formats[count[0]]==37);
    printf("program binary formats=%d\n",count[0]);
    for(GLint i=0;i<count[0];++i)printf("program binary format %d=%x\n",i,formats[i]);

    GLuint original=glCreateProgram();
    glProgramParameteri(original,GL_PROGRAM_BINARY_RETRIEVABLE_HINT,GL_TRUE);
    CHECK(glGetError()==GL_NO_ERROR);
    glAttachShader(original,shader(GL_VERTEX_SHADER,"#version 410\n"
        "void main(){gl_Position=vec4(gl_VertexID==1?3:-1,gl_VertexID==2?3:-1,0,1);}"));
    glAttachShader(original,shader(GL_FRAGMENT_SHADER,"#version 410\n"
        "uniform vec4 paint;out vec4 color;void main(){color=paint;}"));
    glLinkProgram(original);
    GLint linked=0,size=-1;
    glGetProgramiv(original,GL_LINK_STATUS,&linked);
    glGetProgramiv(original,GL_PROGRAM_BINARY_LENGTH,&size);
    CHECK(glGetError()==GL_NO_ERROR && linked && size>=0 && size<16*1024*1024);
    printf("linked program binary length=%d\n",size);
    if(size>0 && size<16*1024*1024){
        GLubyte *binary=malloc((size_t)size+1);
        if(!binary)return 2;
        binary[size]=37;GLsizei actual=-1;GLenum format=0;
        glGetProgramBinary(original,size,&actual,&format,binary);
        CHECK(glGetError()==GL_NO_ERROR && actual>0 && actual<=size && binary[size]==37);
        GLuint restored=glCreateProgram();
        glProgramBinary(restored,format,binary,actual);
        glGetProgramiv(restored,GL_LINK_STATUS,&linked);
        CHECK(glGetError()==GL_NO_ERROR && linked);
        if(linked){
            GLuint vao=0,fb=0,tex=0;
            glGenVertexArrays(1,&vao);glBindVertexArray(vao);
            glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
            glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);
            glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
            CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
            glViewport(0,0,2,2);glUseProgram(restored);
            GLint paint=glGetUniformLocation(restored,"paint");CHECK(paint>=0);
            glUniform4f(paint,1,0,1,1);glDrawArrays(GL_TRIANGLES,0,3);
            GLubyte rgba[4]={0},expected[]={255,0,255,255};
            glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
            CHECK(glGetError()==GL_NO_ERROR && !memcmp(rgba,expected,sizeof(rgba)));
        }
        free(binary);
    }else{
        GLsizei actual=-1;GLenum format=37;GLubyte binary[1]={37};
        glGetProgramBinary(original,sizeof(binary),&actual,&format,binary);
        GLenum error=glGetError();
        printf("get binary with no format: error=%x length=%d format=%x guard=%u\n",
            error,actual,format,binary[0]);
        CHECK(error==GL_INVALID_OPERATION && actual==-1 && format==37 && binary[0]==37);
        glGetProgramBinary(original,0,NULL,&format,NULL);
        CHECK(glGetError()==GL_INVALID_OPERATION && format==37);
    }
    GLuint bad=glCreateProgram();const GLubyte bad_bytes[]={1,2,3,4};
    glProgramBinary(bad,~0u,bad_bytes,sizeof(bad_bytes));
    GLenum invalid=glGetError();glGetProgramiv(bad,GL_LINK_STATUS,&linked);
    printf("unknown program format: error=%x linked=%d\n",invalid,linked);
    CHECK(invalid==GL_INVALID_ENUM && !linked && glGetError()==GL_NO_ERROR);
#ifdef VIM_PROGRAM_BINARY_WIRE
    CHECK(!c->connection->dead);DGDestroyContext(c);
#else
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);
#endif
    printf("program binary failures=%d\n",failures);return failures?1:0;
}
