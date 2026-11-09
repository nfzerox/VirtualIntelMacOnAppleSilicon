/* Native raster/stipple semantics with inaccessible skipped client memory. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <sys/mman.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static unsigned failures;
static void check(int good,const char *name)
{printf("%s %s\n",good?"PASS":"FAIL",name);fflush(stdout);failures+=!good;}
static void unpack(GLint skip,int lsb)
{
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,skip);
    glPixelStorei(GL_UNPACK_ROW_BYTES_APPLE,5);glPixelStorei(GL_UNPACK_LSB_FIRST,lsb);
}
static void render(void)
{
    glEnable(GL_POLYGON_STIPPLE);glColor4f(1,1,1,1);
    glBegin(GL_QUADS);glVertex2f(-1,-1);glVertex2f(1,-1);glVertex2f(1,1);glVertex2f(-1,1);glEnd();
    glDisable(GL_POLYGON_STIPPLE);
}
static void read_image(unsigned char *p)
{glReadPixels(0,0,32,32,GL_RGBA,GL_UNSIGNED_BYTE,p);}
static int patterned(const unsigned char *p)
{
    unsigned white=0,black=0;
    for(unsigned i=0;i<4096;i+=4){white+=p[i]==255&&p[i+1]==255&&p[i+2]==255&&p[i+3]==255;
        black+=!p[i]&&!p[i+1]&&!p[i+2]&&!p[i+3];}
    return white&&black&&white+black==1024;
}
int main(void)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;
    CGLDestroyPixelFormat(pf);
    printf("renderer=%s version=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    GLuint texture=0,fbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,32,32,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"bitmap input framebuffer");
    glViewport(0,0,32,32);glClearColor(0,0,0,0);glColor4f(1,1,1,1);
    size_t prefix=64u*1024u*1024u,page=(size_t)getpagesize();
    unsigned char *guard=mmap(NULL,prefix+page,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);
    if(guard==MAP_FAILED||mprotect(guard+prefix,page,PROT_READ|PROT_WRITE))return 2;
    unsigned char source[160],expected[4096],actual[4096];
    for(unsigned i=0;i<sizeof(source);++i)source[i]=(unsigned char)(i*37+13);
    memcpy(guard+prefix,source,sizeof(source));
    for(int lsb=0;lsb<2;++lsb){
        unpack(3,lsb);glClear(GL_COLOR_BUFFER_BIT);glRasterPos2f(-1,-1);
        glBitmap(32,32,0,0,1,2,source);read_image(expected);
        check(!glGetError()&&patterned(expected),"ordinary bitmap reference has both pixel values");
        unpack((GLint)(prefix*8+3),lsb);glClear(GL_COLOR_BUFFER_BIT);glRasterPos2f(-1,-1);
        glBitmap(32,32,0,0,1,2,guard);read_image(actual);
        check(!glGetError()&&!memcmp(actual,expected,sizeof(actual)),"guarded bitmap matches ordinary pixels");
        GLfloat raster[4]={0};glGetFloatv(GL_CURRENT_RASTER_POSITION,raster);
        GLint skip=0,binding=-1;glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&skip);
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&binding);
        check(skip==(GLint)(prefix*8+3)&&binding==0&&raster[0]==1&&raster[1]==2&&!glGetError(),
            "guarded bitmap preserves state and raster movement");
        unpack(3,lsb);glPolygonStipple(source);glClear(GL_COLOR_BUFFER_BIT);render();read_image(expected);
        check(!glGetError()&&patterned(expected),"ordinary stipple reference has both pixel values");
        unpack((GLint)(prefix*8+3),lsb);glPolygonStipple(guard);
        glClear(GL_COLOR_BUFFER_BIT);render();read_image(actual);
        check(!glGetError()&&!memcmp(actual,expected,sizeof(actual)),"guarded stipple matches ordinary pixels");
        glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&skip);glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&binding);
        check(skip==(GLint)(prefix*8+3)&&binding==0&&!glGetError(),"guarded stipple restores state and binding");
        glNewList(53,GL_COMPILE);glPolygonStipple(guard);glEndList();
        check(!glGetError(),"guarded stipple compiles into display list");
        unpack(0,0);unsigned char zeros[160]={0};glPolygonStipple(zeros);glCallList(53);
        glClear(GL_COLOR_BUFFER_BIT);render();read_image(actual);
        check(!glGetError()&&!memcmp(actual,expected,sizeof(actual)),"display list retains stipple source");
        glDeleteLists(53,1);
        unpack((GLint)(prefix*8+3),lsb);glEnable(0xffffffffu);glPolygonStipple(guard);
        check(glGetError()==GL_INVALID_ENUM&&!glGetError(),"guarded stipple preserves prior error");
    }
    munmap(guard,prefix+page);glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&texture);
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);
    printf("guarded-bitmap failures=%u\n",failures);return failures?1:0;
}
