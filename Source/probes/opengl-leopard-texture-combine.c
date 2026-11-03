/* Finite GPU-pixel qualification of the core 1.3 texture-combine operations
 * Leopard Core Animation requires under the older ARB extension spelling.
 * Run unchanged on native hardware and the forwarded context. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <stdlib.h>
static int failures;
int main(void)
{
    CGLPixelFormatAttribute a[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj f=NULL;CGLContextObj c=NULL;GLint n=0;
    if(CGLChoosePixelFormat(a,&f,&n)||!f||CGLCreateContext(f,NULL,&c)||!c)return 2;
    CGLDestroyPixelFormat(f);if(CGLSetCurrentContext(c))return 2;
    printf("renderer=%s version=%s bits=%u\n",glGetString(GL_RENDERER),glGetString(GL_VERSION),(unsigned)(8*sizeof(void*)));
    GLuint source,target,fb;glGenTextures(1,&source);glGenTextures(1,&target);
    glBindTexture(GL_TEXTURE_2D,target);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fb);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fb);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,target,0);
    if(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT)return 2;
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);glViewport(0,0,4,4);
    glBindTexture(GL_TEXTURE_2D,source);unsigned char texel[]={64,128,192,255};
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,texel);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glEnable(GL_TEXTURE_2D);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_COMBINE);
    glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE0_RGB,GL_TEXTURE);glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE1_RGB,GL_PRIMARY_COLOR);
    glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE2_RGB,GL_CONSTANT);glTexEnvi(GL_TEXTURE_ENV,GL_OPERAND2_RGB,GL_SRC_COLOR);
    GLfloat constant[]={.25f,.25f,.25f,1};
    glTexEnvfv(GL_TEXTURE_ENV,GL_TEXTURE_ENV_COLOR,constant);
    const GLenum modes[]={GL_REPLACE,GL_MODULATE,GL_ADD,GL_ADD_SIGNED,GL_INTERPOLATE,GL_SUBTRACT};
    const int expected[][3]={{64,128,192},{32,64,96},{192,255,255},{64,128,192},{112,128,144},{0,0,64}};
    for(unsigned i=0;i<sizeof(modes)/sizeof(modes[0]);++i){
        glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_RGB,modes[i]);glColor4f(.5f,.5f,.5f,1);
        glBegin(GL_QUADS);glTexCoord2f(0,0);glVertex2f(-1,-1);glVertex2f(1,-1);glVertex2f(1,1);glVertex2f(-1,1);glEnd();
        unsigned char p[4]={0};glReadPixels(2,2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);GLenum e=glGetError();
        int good=!e;for(unsigned k=0;k<3;++k)if(abs((int)p[k]-expected[i][k])>1)good=0;
        printf("%s mode=%x pixel=%u,%u,%u error=%x\n",good?"PASS":"FAIL",modes[i],p[0],p[1],p[2],e);failures+=!good;
    }
    glDeleteFramebuffersEXT(1,&fb);glDeleteTextures(1,&source);glDeleteTextures(1,&target);
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);return failures?1:0;
}
