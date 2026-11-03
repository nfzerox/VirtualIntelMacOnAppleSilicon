/* Native Intel contract: fractional PixelStoref is not a C integer cast.
 * A guard-page-ended RGB upload makes a stale transport shadow observable. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
static int failures;
static void check(int okay,const char *name)
{fprintf(stderr,"%s %s\n",okay?"PASS":"FAIL",name);failures+=!okay;}
static void query(GLenum key,GLint expected,const char *name)
{GLint value=-1;glGetIntegerv(key,&value);check(!glGetError()&&value==expected,name);}
static void upload(const unsigned char *source,const unsigned char *expected)
{
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGB8,2,2,0,GL_RGB,GL_UNSIGNED_BYTE,source);
    check(glGetError()==GL_NO_ERROR,"fractional pixel-store RGB upload accepted");
    unsigned char output[16]={0};glReadPixels(0,0,2,2,GL_RGBA,GL_UNSIGNED_BYTE,output);
    check(!glGetError()&&!memcmp(output,expected,16),"actual GPU texels honor rounded rows and skips");
}
int main(void)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj context=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attrs,&pf,&count)||!pf||CGLCreateContext(pf,NULL,&context)||CGLSetCurrentContext(context))return 2;
    const char *renderer=(const char *)glGetString(GL_RENDERER);fprintf(stderr,"renderer=%s\n",renderer?renderer:"none");
    if(!renderer||!strstr(renderer,"Intel"))return 2;
    GLuint texture=0,fbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGB8,2,2,0,GL_RGB,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,texture,0);
    check(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT,"RGB framebuffer");
    size_t page=getpagesize();unsigned char *allocation=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0);
    if(allocation==MAP_FAILED||mprotect(allocation+page,page,PROT_NONE))return 2;
    unsigned char *source=allocation+page-27;memset(source,0xa5,27);
    const unsigned char expected[]={255,0,0,255,0,255,0,255,0,0,255,255,255,255,0,255};
    const unsigned offsets[]={12,15,21,24};for(unsigned i=0;i<4;++i)memcpy(source+offsets[i],expected+i*4,3);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStoref(GL_UNPACK_ROW_LENGTH,2.6f);
    glPixelStoref(GL_UNPACK_SKIP_ROWS,.6f);glPixelStoref(GL_UNPACK_SKIP_PIXELS,.5f);
    query(GL_UNPACK_ROW_LENGTH,3,"fractional row length rounds up");
    query(GL_UNPACK_SKIP_ROWS,1,"fractional row skip rounds up");
    query(GL_UNPACK_SKIP_PIXELS,1,"half-integer pixel skip rounds away from zero");
    upload(source,expected);
    glPixelStoref(GL_UNPACK_ALIGNMENT,2.6f);check(glGetError()==GL_INVALID_VALUE,"fractional alignment rejected before rounding");
    query(GL_UNPACK_ALIGNMENT,1,"invalid alignment preserves prior state");upload(source,expected);
    glPixelStoref(GL_UNPACK_ROW_LENGTH,-.1f);check(glGetError()==GL_INVALID_VALUE,"negative fractional row length rejected");
    query(GL_UNPACK_ROW_LENGTH,3,"negative fraction preserves row state");upload(source,expected);
    glBegin(GL_POINTS);glPixelStorei(GL_UNPACK_ROW_LENGTH,1);glPixelStoref(GL_UNPACK_SKIP_ROWS,0);glEnd();
    check(glGetError()==GL_INVALID_OPERATION,"pixel-store mutation inside Begin/End rejected");
    query(GL_UNPACK_ROW_LENGTH,3,"rejected integer mutation preserves row state");
    query(GL_UNPACK_SKIP_ROWS,1,"rejected float mutation preserves skip state");upload(source,expected);
    glPixelStoref(GL_UNPACK_ROW_BYTES_APPLE,9);check(glGetError()==GL_INVALID_ENUM,"native PixelStoref rejects APPLE pitch enum");
    query(GL_UNPACK_ROW_BYTES_APPLE,0,"rejected APPLE pitch leaves layout unchanged");upload(source,expected);
    unsigned char packed[27];memset(packed,0xa5,sizeof(packed));
    for(unsigned i=0;i<4;++i)memcpy(packed+offsets[i],expected+i*4,3);
    glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStoref(GL_PACK_ROW_LENGTH,2.6f);
    glPixelStoref(GL_PACK_SKIP_ROWS,.6f);glPixelStoref(GL_PACK_SKIP_PIXELS,.5f);
    query(GL_PACK_ROW_LENGTH,3,"fractional pack row length rounds up");
    query(GL_PACK_SKIP_ROWS,1,"fractional pack row skip rounds up");
    query(GL_PACK_SKIP_PIXELS,1,"fractional pack pixel skip rounds up");
    memset(source,0xa5,27);glReadPixels(0,0,2,2,GL_RGB,GL_UNSIGNED_BYTE,source);
    check(!glGetError()&&!memcmp(source,packed,27),"guarded packed RGB read preserves skipped and padding bytes");
    glPixelStoref(GL_PACK_ALIGNMENT,2.6f);check(glGetError()==GL_INVALID_VALUE,"fractional pack alignment rejected");
    glPixelStoref(GL_PACK_ROW_LENGTH,-.1f);check(glGetError()==GL_INVALID_VALUE,"negative fractional pack row rejected");
    query(GL_PACK_ALIGNMENT,1,"rejected pack alignment preserves state");
    query(GL_PACK_ROW_LENGTH,3,"negative pack fraction preserves row state");
    memset(source,0xa5,27);glReadPixels(0,0,2,2,GL_RGB,GL_UNSIGNED_BYTE,source);
    check(!glGetError()&&!memcmp(source,packed,27),"pack read remains coherent after rejected mutations");
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&texture);
    munmap(allocation,page*2);CGLSetCurrentContext(NULL);CGLDestroyContext(context);CGLDestroyPixelFormat(pf);
    fprintf(stderr,"pixel-store-float failures=%d\n",failures);return !!failures;
}
