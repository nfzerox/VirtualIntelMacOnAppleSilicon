/* Native-host reproducer for disjoint rectangle-texture atlas passes.
 * No guest, bridge, drawable import, or CPU pixel transfer in the draw. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static GLuint shader(GLenum type,const char *source)
{
    GLuint s=glCreateShader(type);glShaderSource(s,1,&source,NULL);glCompileShader(s);
    GLint good=0;glGetShaderiv(s,GL_COMPILE_STATUS,&good);
    if(!good){char log[1024];glGetShaderInfoLog(s,sizeof(log),NULL,log);fprintf(stderr,"%s\n",log);exit(2);}
    return s;
}

static int trial(GLint internal,GLuint program,int mode)
{
    enum { W=3200,H=1600,SOURCE_X=2584,STRIPE=40,ROWS=64 };
    static const char *names[]={"distinct-copy","barrier","flush+barrier","finish+barrier","rebind+barrier"};
    GLuint image[2],fbo[2];glGenTextures(2,image);glGenFramebuffersEXT(2,fbo);
    for(unsigned i=0;i<2;++i){
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB,image[i]);
        glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,internal,W,H,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo[i]);
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,image[i],0);
        if(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT)return 0;
    }
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo[0]);
    glUseProgram(0);glViewport(0,0,W,H);glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);
    glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    for(int i=0;i<3;++i){
        glScissor(SOURCE_X+i*STRIPE,0,STRIPE,ROWS);
        glClearColor(i==0,i==1,i==2,1);glClear(GL_COLOR_BUFFER_BIT);
    }
    glDisable(GL_SCISSOR_TEST);
    if(!mode){
        glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,fbo[1]);
        glBlitFramebufferEXT(SOURCE_X,0,SOURCE_X+3*STRIPE,ROWS,SOURCE_X,0,SOURCE_X+3*STRIPE,ROWS,GL_COLOR_BUFFER_BIT,GL_NEAREST);
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo[0]);
    }else{
        if(mode==2)glFlush();
        if(mode==3)glFinish();
        if(mode==4){glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo[1]);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo[0]);}
        glTextureBarrierNV();
    }
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,image[mode?0:1]);
    glUseProgram(program);glUniform1i(glGetUniformLocation(program,"image"),0);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,W,0,H,-1,1);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glBegin(GL_QUADS);
    glTexCoord2f(SOURCE_X,0);glVertex2f(0,0);
    glTexCoord2f(SOURCE_X+3*STRIPE,0);glVertex2f(3*STRIPE,0);
    glTexCoord2f(SOURCE_X+3*STRIPE,ROWS);glVertex2f(3*STRIPE,ROWS);
    glTexCoord2f(SOURCE_X,ROWS);glVertex2f(0,ROWS);
    glEnd();
    unsigned char pixels[3][4]={{0}};int good=1;
    for(int i=0;i<3;++i){
        glReadPixels(i*STRIPE+STRIPE/2,ROWS/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixels[i]);
        for(int j=0;j<4;++j)good&=pixels[i][j]==(i==j||j==3?255:0);
    }
    GLenum error=glGetError();good&=!error;
    printf("%s %x %s: %u,%u,%u,%u / %u,%u,%u,%u / %u,%u,%u,%u GL=%x\n",good?"PASS":"FAIL",internal,names[mode],
        pixels[0][0],pixels[0][1],pixels[0][2],pixels[0][3],pixels[1][0],pixels[1][1],pixels[1][2],pixels[1][3],
        pixels[2][0],pixels[2][1],pixels[2][2],pixels[2][3],error);
    glUseProgram(0);glDeleteFramebuffersEXT(2,fbo);glDeleteTextures(2,image);return good;
}

int main(void)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFAAllowOfflineRenderers,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attrs,&format,&count)||!format||CGLCreateContext(format,NULL,&context)||CGLSetCurrentContext(context))return 2;
    printf("renderer=%s version=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    const char *extensions=(const char *)glGetString(GL_EXTENSIONS);
    printf("GL_APPLE_float_pixels=%d GL_ARB_texture_float=%d\n",
           extensions&&strstr(extensions,"GL_APPLE_float_pixels")!=NULL,
           extensions&&strstr(extensions,"GL_ARB_texture_float")!=NULL);
    for(unsigned bits=64;bits<=128;bits*=2){
        CGLPixelFormatAttribute floating[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFAColorFloat,
            kCGLPFAColorSize,(CGLPixelFormatAttribute)bits,0};
        CGLPixelFormatObj candidate=NULL;GLint screens=0,reported=0,size=0;
        CGLError error=CGLChoosePixelFormat(floating,&candidate,&screens);
        if(candidate){
            CGLDescribePixelFormat(candidate,0,kCGLPFAColorFloat,&reported);
            CGLDescribePixelFormat(candidate,0,kCGLPFAColorSize,&size);
            CGLDestroyPixelFormat(candidate);
        }
        printf("float CGL format %u: error=%d screens=%d float=%d color-size=%d\n",
               bits,error,screens,reported,size);
    }
    GLuint program=glCreateProgram();
    glAttachShader(program,shader(GL_VERTEX_SHADER,"#version 120\nvarying vec2 uv;void main(){gl_Position=ftransform();uv=gl_MultiTexCoord0.xy;}"));
    glAttachShader(program,shader(GL_FRAGMENT_SHADER,"#version 120\n#extension GL_ARB_texture_rectangle : enable\nvarying vec2 uv;uniform sampler2DRect image;void main(){gl_FragColor=texture2DRect(image,uv);}"));
    glLinkProgram(program);GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);if(!linked)return 2;
    const GLint types[]={GL_RGBA8,GL_RGBA16F_ARB};int good=1;
    for(unsigned i=0;i<sizeof(types)/sizeof(*types);++i)for(int mode=0;mode<5;++mode)good&=trial(types[i],program,mode);
    glDeleteProgram(program);CGLSetCurrentContext(NULL);CGLDestroyContext(context);CGLDestroyPixelFormat(format);
    return !good;
}
