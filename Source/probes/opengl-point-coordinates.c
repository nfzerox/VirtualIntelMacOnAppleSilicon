// Verify both point-sprite coordinate origins using the installed GL driver.
#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static GLuint compile(GLenum type,const char *source) {
    GLuint shader=glCreateShader(type);glShaderSource(shader,1,&source,NULL);
    glCompileShader(shader);GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[2048];glGetShaderInfoLog(shader,sizeof(log),NULL,log);fprintf(stderr,"%s\n",log);exit(2);}
    return shader;
}
int main(void) {
    alarm(60);setbuf(stdout,NULL);
    CGLPixelFormatAttribute attrs[]={kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,kCGLPFAAccelerated,0};
    CGLPixelFormatObj format=NULL;GLint count=0,accelerated=0;
    if(CGLChoosePixelFormat(attrs,&format,&count)||!format)return 2;
    CGLDescribePixelFormat(format,0,kCGLPFAAccelerated,&accelerated);
    if(!accelerated)return 3;
    CGLContextObj context=NULL;
    if(CGLCreateContext(format,NULL,&context)||CGLSetCurrentContext(context))return 2;
    printf("accelerated=%d renderer=%s\n",accelerated,glGetString(GL_RENDERER));
    GLuint program=glCreateProgram();
    glAttachShader(program,compile(GL_VERTEX_SHADER,"#version 150\nvoid main(){gl_Position=vec4(0,0,0,1);gl_PointSize=32.0;}"));
    glAttachShader(program,compile(GL_FRAGMENT_SHADER,"#version 150\nout vec4 c;void main(){c=vec4(gl_PointCoord,0.0,1.0);}"));
    glLinkProgram(program);GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);if(!linked)return 2;
    GLuint texture,framebuffer,vao;
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,64,64,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffers(1,&framebuffer);glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)return 2;
    glGenVertexArrays(1,&vao);glBindVertexArray(vao);glUseProgram(program);
    glEnable(GL_PROGRAM_POINT_SIZE);glDisable(GL_DITHER);glViewport(0,0,64,64);
    unsigned total=0;
    for(unsigned run=0;run<4;run++) {
        int lower=run&1;
        glPointParameteri(GL_POINT_SPRITE_COORD_ORIGIN,lower?GL_LOWER_LEFT:GL_UPPER_LEFT);
        glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);glDrawArrays(GL_POINTS,0,1);
        unsigned char pixels[64*64*4];glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        GLenum error=glGetError();unsigned bad=0;
        for(unsigned y=20;y<44;y++)for(unsigned x=20;x<44;x++) {
            int r=(int)((x-16+0.5)*255/32+0.5);
            double t=(y-16+0.5)/32;
            int g=(int)((lower?t:1-t)*255+0.5);
            unsigned char *p=pixels+4*(y*64+x);
            if(abs((int)p[0]-r)>1||abs((int)p[1]-g)>1||p[2]!=0||p[3]!=255)bad++;
        }
        printf("origin=%s checked=576 bad=%u error=0x%x sample=%u,%u\n",
            lower?"lower-left":"upper-left",bad,error,pixels[4*(20*64+20)],pixels[4*(20*64+20)+1]);
        total+=bad+(error!=0);
    }
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);CGLDestroyPixelFormat(format);
    return total?1:0;
}
