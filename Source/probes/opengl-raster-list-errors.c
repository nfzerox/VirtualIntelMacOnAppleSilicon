/* Rejected public DrawPixels calls must not read input, change the framebuffer,
 * or change error timing when compiled. Run unchanged on the reference/guest. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
static unsigned checks,failures;
static int pbo_input;
static void check(int good,const char *what)
{++checks;failures+=!good;if(!good)printf("FAIL %s\n",what);}
static void run(GLsizei width,GLsizei height,GLenum format,GLenum type,
                GLenum expected,GLenum mode,const void *pixels)
{
    GLuint list=0;
    if(mode){list=glGenLists(1);check(list!=0,"allocate list");glNewList(list,mode);}
    glDrawPixels(width,height,format,type,pixels);
    if(mode)glEndList();
    GLenum error=glGetError();
    printf("width=%d height=%d format=%x type=%x mode=%x initial-error=%x\n",
        width,height,format,type,mode,error);
    check(error==(mode==GL_COMPILE?GL_NO_ERROR:expected),"initial/compile error timing");
    if(mode){glCallList(list);check(glGetError()==expected,"list playback error");glDeleteLists(list,1);}
    GLubyte output[4]={0};glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,output);
    check(!glGetError() && output[0]==255 && !output[1] && !output[2] && output[3]==255,
        "rejected draw preserves pixels and usable connection");
}
int main(int argc,char **argv)
{
    if(argc==2&&!strcmp(argv[1],"--pbo"))pbo_input=1;else if(argc!=1)return 2;
    const CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj context=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attrs,&pf,&count)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&context)||CGLSetCurrentContext(context))return 2;
    CGLDestroyPixelFormat(pf);
    printf("renderer=%s version=%s input=%s\n",glGetString(GL_RENDERER),
        glGetString(GL_VERSION),pbo_input?"pbo":"cpu");
    if(pbo_input){
        unsigned major=0,minor=0;sscanf((const char *)glGetString(GL_VERSION),"%u.%u",&major,&minor);
        const char *extensions=(const char *)glGetString(GL_EXTENSIONS);
        if((major<2||(major==2&&minor<1))&&(!extensions||!strstr(extensions,"GL_ARB_pixel_buffer_object"))){
            puts("UNSUPPORTED pixel buffer objects");CGLSetCurrentContext(NULL);CGLDestroyContext(context);return 77;}
    }
    GLuint fb=0,rb=0,pbo=0;
    glGenFramebuffersEXT(1,&fb);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fb);
    glGenRenderbuffersEXT(1,&rb);glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,rb);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT,GL_RGBA8,8,8);
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_RENDERBUFFER_EXT,rb);
    check(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT,"complete framebuffer");
    glViewport(0,0,8,8);glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glWindowPos2i(0,0);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_PACK_ALIGNMENT,1);
    size_t page=(size_t)getpagesize();
    void *guard=mmap(NULL,page,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);
    if(guard==MAP_FAILED)return 2;
    if(pbo_input){glGenBuffers(1,&pbo);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,pbo);
        glBufferData(GL_PIXEL_UNPACK_BUFFER,4096,NULL,GL_STATIC_DRAW);}
    check(!glGetError(),"initial state accepted");
    const GLenum modes[]={0,GL_COMPILE,GL_COMPILE_AND_EXECUTE};
    const GLenum types[]={GL_UNSIGNED_BYTE_3_3_2,GL_UNSIGNED_BYTE_2_3_3_REV,
        GL_UNSIGNED_SHORT_5_6_5,GL_UNSIGNED_SHORT_5_6_5_REV,
        GL_UNSIGNED_SHORT_4_4_4_4,GL_UNSIGNED_SHORT_4_4_4_4_REV,
        GL_UNSIGNED_SHORT_5_5_5_1,GL_UNSIGNED_SHORT_1_5_5_5_REV,
        GL_UNSIGNED_INT_8_8_8_8,GL_UNSIGNED_INT_8_8_8_8_REV,
        GL_UNSIGNED_INT_10_10_10_2,GL_UNSIGNED_INT_2_10_10_10_REV};
    const GLenum formats[]={GL_RGB,GL_RGBA,GL_BGRA,GL_BGR,GL_LUMINANCE,GL_DEPTH_COMPONENT,GL_STENCIL_INDEX};
    for(unsigned t=0;t<sizeof(types)/sizeof(types[0]);++t)
        for(unsigned f=0;f<sizeof(formats)/sizeof(formats[0]);++f){
            /* GL2.1 packed RGB versus RGBA/BGRA legality, not a pixel oracle. */
            if(t<4?formats[f]==GL_RGB:(formats[f]==GL_RGBA||formats[f]==GL_BGRA))continue;
            for(unsigned m=0;m<3;++m)for(unsigned pointer=0;pointer<(pbo_input?1u:2u);++pointer)
                run(8,8,formats[f],types[t],GL_INVALID_OPERATION,modes[m],pointer?guard:NULL);
        }
    for(unsigned m=0;m<3;++m){
        const void *pixels=pbo_input?NULL:guard;
        run(-1,8,GL_RGBA,GL_UNSIGNED_BYTE,GL_INVALID_VALUE,modes[m],pixels);
        run(8,-1,GL_RGBA,GL_UNSIGNED_BYTE,GL_INVALID_VALUE,modes[m],pixels);
        run(8,8,0xdead,GL_UNSIGNED_BYTE,GL_INVALID_ENUM,modes[m],pixels);
        run(8,8,GL_RGBA,0xdead,GL_INVALID_ENUM,modes[m],pixels);
    }
    if(pbo){glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glDeleteBuffers(1,&pbo);}
    glDeleteFramebuffersEXT(1,&fb);glDeleteRenderbuffersEXT(1,&rb);munmap(guard,page);
    check(!glGetError(),"cleanup accepted");
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);
    printf("raster-list-errors checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
