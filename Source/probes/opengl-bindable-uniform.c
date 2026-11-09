/* Native and wire EXT_bindable_uniform signed offset and buffer-backed draw. */
#ifdef VIM_BINDABLE_UNIFORM_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;} } while(0)
int main(void)
{
#ifdef VIM_BINDABLE_UNIFORM_WIRE
    const int attrs[]={0x56494d02,0x1000,0};unsigned char screen[16*16*4]={0};
    struct DGContext *ctx=DGCreateContext(attrs,NULL);
    if(!ctx || !DGMakeCurrent(ctx,screen,GL_UNSIGNED_BYTE,16,16))return 2;
#else
    const CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj ctx=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&ctx)||CGLSetCurrentContext(ctx))return 2;
    CGLDestroyPixelFormat(pf);
#endif
    const char *ext=(const char *)glGetString(GL_EXTENSIONS);
    printf("bindable renderer=%s\n",glGetString(GL_RENDERER));
    if(!ext || !strstr(ext,"GL_EXT_bindable_uniform"))return 77;
    GLint max_size=0,max_count=0;
    glGetIntegerv(GL_MAX_BINDABLE_UNIFORM_SIZE_EXT,&max_size);
    glGetIntegerv(GL_MAX_FRAGMENT_BINDABLE_UNIFORMS_EXT,&max_count);
    CHECK(glGetError()==GL_NO_ERROR && max_size>=16 && max_count>0);
    GLuint shader=glCreateShader(GL_FRAGMENT_SHADER),program=glCreateProgram();
    const GLchar *source="#version 120\n#extension GL_EXT_bindable_uniform : require\n"
        "bindable uniform vec4 colour[2];\n"
        "void main(){gl_FragColor=gl_FragCoord.x<8.0 ? colour[0] : colour[1];}\n";
    glShaderSource(shader,1,&source,NULL);glCompileShader(shader);
    glAttachShader(program,shader);glLinkProgram(program);
    GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);
    if(!linked){char log[512]={0};glGetShaderInfoLog(shader,sizeof(log),NULL,log);fprintf(stderr,"link failed: %s\n",log);return 1;}
    GLint first=glGetUniformLocationARB((GLhandleARB)(uintptr_t)program,"colour[0]");
    GLint second=glGetUniformLocationARB((GLhandleARB)(uintptr_t)program,"colour[1]");
    GLint size=glGetUniformBufferSizeEXT(program,first);
    GLintptr offset0=glGetUniformOffsetEXT(program,first),offset1=glGetUniformOffsetEXT(program,second);
    printf("bindable offsets=%ld,%ld buffer size=%d\n",(long)offset0,(long)offset1,size);
    CHECK(glGetError()==GL_NO_ERROR);
    if(size<32 || size>max_size || offset0<0 || offset1<0 || offset0>size-16 || offset1>size-16 ||
       offset1-offset0<16)return 1;
    GLintptr invalid=glGetUniformOffsetEXT(program,-1);
    GLenum error=glGetError();printf("bindable invalid offset=%ld error=%04x\n",(long)invalid,error);
    /* Apple M4 returns zero here; other native drivers may use the EXT -1. */
    CHECK((invalid==0 || invalid==-1) && error==GL_INVALID_OPERATION);
    GLuint buffer=0,rb=0,fb=0;
    glGenBuffers(1,&buffer);glBindBuffer(GL_UNIFORM_BUFFER_EXT,buffer);
    GLint binding=0;glGetIntegerv(GL_UNIFORM_BUFFER_BINDING_EXT,&binding);
    CHECK(binding==(GLint)buffer);
    glBufferData(GL_UNIFORM_BUFFER_EXT,size,NULL,GL_DYNAMIC_DRAW);
    const GLfloat red[]={1,0,0,1},green[]={0,1,0,1};
    glBufferSubData(GL_UNIFORM_BUFFER_EXT,offset0,sizeof(red),red);
    glBufferSubData(GL_UNIFORM_BUFFER_EXT,offset1,sizeof(green),green);
    glUniformBufferEXT(program,first,buffer);glUseProgram(program);
    glGenRenderbuffersEXT(1,&rb);glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,rb);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT,GL_RGBA8,16,16);
    glGenFramebuffersEXT(1,&fb);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fb);
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_RENDERBUFFER_EXT,rb);
    CHECK(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    glViewport(0,0,16,16);
    glBegin(GL_QUADS);glVertex2f(-1,-1);glVertex2f(1,-1);glVertex2f(1,1);glVertex2f(-1,1);glEnd();
    GLubyte a[4]={0},b[4]={0};
    glReadPixels(3,3,1,1,GL_RGBA,GL_UNSIGNED_BYTE,a);glReadPixels(12,3,1,1,GL_RGBA,GL_UNSIGNED_BYTE,b);
    error=glGetError();
    printf("bindable red=%u,%u green=%u,%u error=%04x\n",a[0],a[1],b[0],b[1],error);
    CHECK(!error && a[0]==255 && a[1]==0 && b[0]==0 && b[1]==255);
    return failures!=0;
}
