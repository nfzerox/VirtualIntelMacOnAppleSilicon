/* GL 2.1 pixel-map state, exact native conversions and CPU/PBO boundaries. */
#ifdef VIM_PIXEL_MAP_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#endif
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;} } while(0)
static void verify(void)
{
    const GLfloat expected[]={1.0f,0.0f,1.0f};
    const GLuint uints[]={0,~0u,0};
    const GLushort shorts[]={0xffff,0,0xffff};
    const GLenum key=GL_PIXEL_MAP_R_TO_R;
    size_t page=(size_t)getpagesize();
    char *guard=mmap(NULL,2*page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    CHECK(guard!=MAP_FAILED);
    if(guard==MAP_FAILED)return;
    if(mprotect(guard+page,page,PROT_NONE)){++failures;munmap(guard,2*page);return;}
    GLfloat *edge=(GLfloat *)(guard+page-sizeof(expected));
    memcpy(edge,expected,sizeof(expected));
    /* Pixel storage state doesn't affect table layout. */
    glPixelStorei(GL_UNPACK_SKIP_PIXELS,7);glPixelStorei(GL_PACK_SKIP_ROWS,3);
    glPixelMapfv(key,3,edge);memset(edge,0,sizeof(expected));
    glGetPixelMapfv(key,edge);
    CHECK(glGetError()==GL_NO_ERROR && !memcmp(edge,expected,sizeof(expected)));
    GLint count=0;glGetIntegerv(GL_PIXEL_MAP_R_TO_R_SIZE,&count);
    CHECK(count==3 && glGetError()==GL_NO_ERROR);
    GLuint out_ui[4]={1,1,1,37};GLushort out_us[4]={1,1,1,37};
    glPixelMapuiv(key,3,uints);glGetPixelMapuiv(key,out_ui);
    CHECK(glGetError()==GL_NO_ERROR && !memcmp(out_ui,uints,sizeof(uints)) && out_ui[3]==37);
    glPixelMapusv(key,3,shorts);glGetPixelMapusv(key,out_us);
    CHECK(glGetError()==GL_NO_ERROR && !memcmp(out_us,shorts,sizeof(shorts)) && out_us[3]==37);
    GLfloat saved[3]={.25f,.5f,.75f};
#ifdef VIM_PIXEL_MAP_WIRE
    glGetPixelMapfv(0,(void *)(guard+page));CHECK(glGetError()==GL_INVALID_ENUM);
    glPixelMapfv(GL_PIXEL_MAP_I_TO_R,3,(const void *)(guard+page));CHECK(glGetError()==GL_INVALID_VALUE);
    glBegin(GL_POINTS);glGetPixelMapfv(key,(void *)(guard+page));glEnd();CHECK(glGetError()==GL_INVALID_OPERATION);
#else
    /* Some native compatibility drivers do not reject these before touching
     * client storage. The valid guard-page cases above are portable. */
    glGetPixelMapfv(0,saved);printf("native invalid map error=%04x\n",glGetError());
    glPixelMapfv(GL_PIXEL_MAP_I_TO_R,3,saved);printf("native non-power-of-two error=%04x\n",glGetError());
    glBegin(GL_POINTS);glGetPixelMapfv(key,saved);glEnd();printf("native begin/end error=%04x\n",glGetError());
#endif
    munmap(guard,2*page);

    GLuint buffers[2]={0};glGenBuffers(2,buffers);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,buffers[0]);
    const GLfloat upload[]={.25f,1.0f,0.0f,1.0f};
    glBufferData(GL_PIXEL_UNPACK_BUFFER,sizeof(upload),NULL,GL_STATIC_DRAW);
    glBufferSubData(GL_PIXEL_UNPACK_BUFFER,0,sizeof(upload),upload);
    glPixelMapfv(key,3,(const void *)sizeof(GLfloat));
    CHECK(glGetError()==GL_NO_ERROR);
    glPixelMapfv(key,3,(const void *)(2*sizeof(GLfloat)));CHECK(glGetError()==GL_INVALID_OPERATION);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    glGetPixelMapfv(key,saved);
    CHECK(glGetError()==GL_NO_ERROR && !memcmp(saved,expected,sizeof(expected)));

    glBindBuffer(GL_PIXEL_PACK_BUFFER,buffers[1]);
    const GLushort initial[]={11,12,13,14,15};
    glBufferData(GL_PIXEL_PACK_BUFFER,sizeof(initial),initial,GL_STATIC_DRAW);
    glGetPixelMapusv(key,(void *)sizeof(GLushort));
    CHECK(glGetError()==GL_NO_ERROR);
    glGetPixelMapusv(key,(void *)(3*sizeof(GLushort)));CHECK(glGetError()==GL_INVALID_OPERATION);
    GLushort result[5]={0};glGetBufferSubData(GL_PIXEL_PACK_BUFFER,0,sizeof(result),result);
    CHECK(glGetError()==GL_NO_ERROR && result[0]==initial[0] && result[4]==initial[4] &&
          !memcmp(result+1,shorts,sizeof(shorts)));
    glGetPixelMapusv(key,NULL);CHECK(glGetError()==GL_NO_ERROR);
    glGetBufferSubData(GL_PIXEL_PACK_BUFFER,0,sizeof(result),result);
    CHECK(glGetError()==GL_NO_ERROR && !memcmp(result,shorts,sizeof(shorts)) && result[4]==initial[4]);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glDeleteBuffers(2,buffers);
}
int main(void)
{
#ifdef VIM_PIXEL_MAP_WIRE
    const int attrs[]={0x56494d02,0x1000,0};unsigned char screen[16*16*4]={0};
    struct DGContext *c=DGCreateContext(attrs,NULL);
    if(!c || !DGMakeCurrent(c,screen,GL_UNSIGNED_BYTE,16,16))return 2;
    CHECK(c->connection->features2&DG_FEATURE2_PIXEL_MAPS);
#else
    const CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;
    CGLDestroyPixelFormat(pf);
#endif
    printf("pixel-map renderer=%s\n",glGetString(GL_RENDERER));verify();
#ifdef VIM_PIXEL_MAP_WIRE
    CHECK(!c->connection->dead);DGDestroyContext(c);
#else
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);
#endif
    printf("pixel-map failures=%d\n",failures);return failures?1:0;
}
