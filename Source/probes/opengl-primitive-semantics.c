// Validate semantics behind Metal's private restart/provoking-vertex warnings.
#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
static GLuint shader(GLenum type,const char *s) {
    GLuint v=glCreateShader(type);glShaderSource(v,1,&s,NULL);glCompileShader(v);
    GLint ok=0;glGetShaderiv(v,GL_COMPILE_STATUS,&ok);if(!ok){char log[2048];glGetShaderInfoLog(v,sizeof(log),NULL,log);fprintf(stderr,"%s\n",log);exit(2);}return v;
}
static unsigned check(unsigned x,unsigned y,unsigned r,unsigned g,unsigned b) {
    unsigned char p[4]={0};glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    GLenum e=glGetError();printf("pixel %u,%u = %u,%u,%u,%u error=0x%x\n",x,y,p[0],p[1],p[2],p[3],e);
    return e||p[0]!=r||p[1]!=g||p[2]!=b||p[3]!=255;
}
int main(void) {
    alarm(60);setbuf(stdout,NULL);
    CGLPixelFormatAttribute a[]={kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,kCGLPFAAccelerated,0};
    CGLPixelFormatObj f=NULL;CGLContextObj c=NULL;GLint n=0,accelerated=0;
    if(CGLChoosePixelFormat(a,&f,&n)||!f)return 2;
    CGLDescribePixelFormat(f,0,kCGLPFAAccelerated,&accelerated);if(!accelerated)return 3;
    if(CGLCreateContext(f,NULL,&c)||CGLSetCurrentContext(c))return 2;
    printf("renderer=%s\n",glGetString(GL_RENDERER));
    GLuint p=glCreateProgram();
    double z=0;const char *requested=getenv("PVG_GL_Z");
    if(requested){char *end=NULL;z=strtod(requested,&end);if(!*requested||*end||!(z>=-.5&&z<=.5))return 64;}
    char vertex[1024];snprintf(vertex,sizeof(vertex),"#version 150\nin vec2 pos;uniform int solid;flat out vec3 color;void main(){gl_Position=vec4(pos,%.9f,1);color=solid!=0?vec3(1,0,0):vec3(gl_VertexID==0?1:0,gl_VertexID==1?1:0,gl_VertexID==2?1:0);}",z);
    glAttachShader(p,shader(GL_VERTEX_SHADER,vertex));
    glAttachShader(p,shader(GL_FRAGMENT_SHADER,"#version 150\nflat in vec3 color;out vec4 outColor;void main(){outColor=vec4(color,1);}"));
    glBindAttribLocation(p,0,"pos");glLinkProgram(p);GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok)return 2;
    GLuint t,fb,vao,vbo,ibo;
    glGenTextures(1,&t);glBindTexture(GL_TEXTURE_2D,t);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,64,64,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t,0);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)return 2;
    glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,NULL);glEnableVertexAttribArray(0);glUseProgram(p);
    GLint solid=glGetUniformLocation(p,"solid");if(solid<0)return 2;
    glDisable(GL_DITHER);glViewport(0,0,64,64);glClearColor(0,1,1,1);
    float triangle[]={-.8f,-.8f,.8f,-.8f,0,.8f};glBufferData(GL_ARRAY_BUFFER,sizeof(triangle),triangle,GL_STATIC_DRAW);
    unsigned bad=0;
    for(unsigned first=0;first<2;first++) {
        glProvokingVertex(first?GL_FIRST_VERTEX_CONVENTION:GL_LAST_VERTEX_CONVENTION);
        glClear(GL_COLOR_BUFFER_BIT);glDrawArrays(GL_TRIANGLES,0,3);
        printf("provoking=%s\n",first?"first":"last");bad+=check(32,32,first?255:0,0,first?0:255);
    }
    float separated[]={-.9f,-.8f,-.5f,.8f,-.1f,-.8f,.1f,-.8f,.5f,.8f,.9f,-.8f};
    unsigned short indices[]={0,1,2,65535,3,4,5};
    glBufferData(GL_ARRAY_BUFFER,sizeof(separated),separated,GL_STATIC_DRAW);
    glGenBuffers(1,&ibo);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ibo);glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(indices),indices,GL_STATIC_DRAW);
    glUniform1i(solid,1);glEnable(GL_PRIMITIVE_RESTART);glPrimitiveRestartIndex(65535);
    glClear(GL_COLOR_BUFFER_BIT);glDrawElements(GL_TRIANGLE_STRIP,7,GL_UNSIGNED_SHORT,NULL);
    puts("primitive restart two strips");bad+=check(16,24,255,0,0);bad+=check(48,24,255,0,0);bad+=check(32,24,0,255,255);
    printf("bad=%u\n",bad);CGLSetCurrentContext(NULL);CGLDestroyContext(c);CGLDestroyPixelFormat(f);return bad?1:0;
}
