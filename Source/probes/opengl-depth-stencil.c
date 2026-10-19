// Hardware-only semantic test: depth rejection, disabled depth writes,
// stencil replacement/equality, and optional multisample resolve.
#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static GLuint compile(GLenum stage,const char *source) {
    GLuint s=glCreateShader(stage);glShaderSource(s,1,&source,NULL);glCompileShader(s);
    GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok) {char log[2048];glGetShaderInfoLog(s,sizeof(log),NULL,log);fprintf(stderr,"%s\n",log);exit(2);}
    return s;
}
static GLint zloc,cloc;
static void draw(float z,float r,float g,float b) {
    glUniform1f(zloc,z);glUniform4f(cloc,r,g,b,1);glDrawArrays(GL_TRIANGLES,0,3);
}
int main(int argc,char **argv) {
    setbuf(stdout,NULL);
    int samples=1,noDither=0;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--msaa"))samples=4;
        else if(!strcmp(argv[i],"--msaa8"))samples=8;
        else if(!strcmp(argv[i],"--no-dither"))noDither=1;
        else return 64;
    }
    CGLPixelFormatAttribute attrs[]={kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,kCGLPFAAccelerated,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj ctx=NULL;GLint n=0,accel=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf||CGLCreateContext(pf,NULL,&ctx)||CGLSetCurrentContext(ctx)) return 2;
    CGLDescribePixelFormat(pf,0,kCGLPFAAccelerated,&accel);if(!accel) return 3;
    printf("accelerated=%d renderer=%s samples=%d\n",accel,glGetString(GL_RENDERER),samples);
    if(noDither)glDisable(GL_DITHER);
    GLuint vs=compile(GL_VERTEX_SHADER,"#version 150\nuniform float z;void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,z,1.0);}");
    GLuint fs=compile(GL_FRAGMENT_SHADER,"#version 150\nuniform vec4 color;out vec4 c;void main(){c=color;}");
    GLuint program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);
    GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);if(!linked) return 2;
    glUseProgram(program);zloc=glGetUniformLocation(program,"z");cloc=glGetUniformLocation(program,"color");
    if(zloc<0||cloc<0)return 2;
    GLuint vao,fb,color,ds;glGenVertexArrays(1,&vao);glBindVertexArray(vao);
    glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);
    glGenRenderbuffers(1,&color);glBindRenderbuffer(GL_RENDERBUFFER,color);
    if(samples>1)glRenderbufferStorageMultisample(GL_RENDERBUFFER,samples,GL_RGBA8,64,64);
    else glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA8,64,64);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,color);
    glGenRenderbuffers(1,&ds);glBindRenderbuffer(GL_RENDERBUFFER,ds);
    if(samples>1)glRenderbufferStorageMultisample(GL_RENDERBUFFER,samples,GL_DEPTH24_STENCIL8,64,64);
    else glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,64,64);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,ds);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)return 2;
    glViewport(0,0,64,64);glClearColor(0,0,0,1);glClearDepth(1);glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);
    draw(0,1,0,0); // red, depth .5
    draw(.5,1,0,1); // behind: must be rejected
    glDepthMask(GL_FALSE);draw(-.5,0,1,0); // green, must not change depth
    glDepthMask(GL_TRUE);draw(-.25,0,0,1); // blue passes against .5, not .25
    glDisable(GL_DEPTH_TEST);glEnable(GL_STENCIL_TEST);glEnable(GL_SCISSOR_TEST);
    glScissor(0,0,32,64);glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
    glStencilFunc(GL_ALWAYS,1,255);glStencilOp(GL_KEEP,GL_KEEP,GL_REPLACE);draw(0,0,0,0);
    glDisable(GL_SCISSOR_TEST);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glStencilMask(0);glStencilFunc(GL_EQUAL,1,255);glStencilOp(GL_KEEP,GL_KEEP,GL_KEEP);draw(0,1,1,0);
    GLuint resolve=0,rb=0;
    if(samples>1) {
        glGenFramebuffers(1,&resolve);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,resolve);
        glGenRenderbuffers(1,&rb);glBindRenderbuffer(GL_RENDERBUFFER,rb);
        glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA8,64,64);
        glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,rb);
        if(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)return 2;
        glBlitFramebuffer(0,0,64,64,0,0,64,64,GL_COLOR_BUFFER_BIT,GL_NEAREST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,resolve);
    }
    unsigned char pixels[64*64*4]={0};glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    GLenum error=glGetError();unsigned bad=0;
    for(unsigned i=0;i<4096;i++) {
        int left=i%64<32;unsigned char *p=pixels+4*i;
        if(p[0]!=(left?255:0)||p[1]!=(left?255:0)||p[2]!=(left?0:255)||p[3]!=255)bad++;
    }
    printf("bad=%u/4096 error=0x%x left=%u,%u,%u right=%u,%u,%u\n",bad,error,pixels[0],pixels[1],pixels[2],pixels[128],pixels[129],pixels[130]);
    glDeleteFramebuffers(1,&fb);glDeleteRenderbuffers(1,&color);glDeleteRenderbuffers(1,&ds);
    if(resolve)glDeleteFramebuffers(1,&resolve);if(rb)glDeleteRenderbuffers(1,&rb);
    glDeleteVertexArrays(1,&vao);glDeleteProgram(program);glDeleteShader(vs);glDeleteShader(fs);
    CGLSetCurrentContext(NULL);CGLDestroyContext(ctx);CGLDestroyPixelFormat(pf);
    return bad||error?1:0;
}
