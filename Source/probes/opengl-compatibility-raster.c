/* Native and wire compatibility raster and stipple pointer codecs. */
#ifdef VIM_COMPAT_RASTER_WIRE
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
static void read_row(GLint x,GLint y,const GLubyte *expected,GLsizei width)
{
    GLubyte row[16]={0};
    glReadPixels(x,y,width,1,GL_RGBA,GL_UNSIGNED_BYTE,row);
    CHECK(glGetError()==GL_NO_ERROR && !memcmp(row,expected,(size_t)width*4));
    printf("raster row y=%d:",y);
    for(int i=0;i<width*4;++i)printf(" %02x",row[i]);
    putchar('\n');
}
static void verify(void)
{
    GLuint rb=0,fb=0;
    glGenRenderbuffersEXT(1,&rb);glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,rb);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT,GL_RGBA8,16,16);
    glGenFramebuffersEXT(1,&fb);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fb);
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_RENDERBUFFER_EXT,rb);
    CHECK(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    glViewport(0,0,16,16);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    size_t page=(size_t)getpagesize();
    char *guard=mmap(NULL,2*page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    CHECK(guard!=MAP_FAILED);
    if(guard==MAP_FAILED)return;
    if(mprotect(guard+page,page,PROT_NONE)){++failures;munmap(guard,2*page);return;}
    GLubyte *bitmap=(GLubyte *)(guard+page-10);
    memset(bitmap,0,10);bitmap[4]=0x80;bitmap[5]=0x02;bitmap[9]=0x01;
    glPixelStorei(GL_UNPACK_ALIGNMENT,4);glPixelStorei(GL_UNPACK_ROW_LENGTH,19);
    glPixelStorei(GL_UNPACK_SKIP_ROWS,1);glPixelStorei(GL_UNPACK_SKIP_PIXELS,7);
    glPixelStorei(GL_UNPACK_LSB_FIRST,GL_TRUE);
    glColor4ub(255,0,0,255);glWindowPos2i(2,2);
    glBitmap(3,2,0,0,1,0,bitmap);
    const GLubyte first[]={255,0,0,255,0,0,0,255,255,0,0,255};
    const GLubyte second[]={0,0,0,255,255,0,0,255,0,0,0,255};
    read_row(2,2,first,3);read_row(2,3,second,3);
    GLfloat position[4]={0};glGetFloatv(GL_CURRENT_RASTER_POSITION,position);
    CHECK(position[0]==3 && position[1]==2 && glGetError()==GL_NO_ERROR);
    glBitmap(0,0,0,0,2,0,NULL);glGetFloatv(GL_CURRENT_RASTER_POSITION,position);
    CHECK(position[0]==5 && position[1]==2 && glGetError()==GL_NO_ERROR);

    const GLubyte rowbytes_bitmap[]={1,0,0,2,4,0,0,0};
    glPixelStorei(GL_UNPACK_ROW_BYTES_APPLE,3);
    glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
    glWindowPos2i(2,10);glBitmap(3,2,0,0,0,0,rowbytes_bitmap);
    GLubyte rb_row[12]={0};glReadPixels(2,11,3,1,GL_RGBA,GL_UNSIGNED_BYTE,rb_row);
    printf("bitmap row-bytes marker: %u %u %u\n",rb_row[0],rb_row[4],rb_row[8]);
    CHECK(glGetError()==GL_NO_ERROR && rb_row[0]==0 && rb_row[4]==255 && rb_row[8]==0);
    glPixelStorei(GL_UNPACK_ROW_BYTES_APPLE,0);glPixelStorei(GL_UNPACK_SKIP_ROWS,1);

    glPixelStorei(GL_UNPACK_ROW_LENGTH,4);glPixelStorei(GL_UNPACK_SKIP_PIXELS,1);
    GLubyte *pixels=(GLubyte *)(guard+page-44);
    memset(pixels,0,44);pixels[21]=255;pixels[23]=255;pixels[26]=255;pixels[27]=255;
    pixels[36]=255;pixels[39]=255;pixels[40]=255;pixels[41]=255;pixels[43]=255;
    glWindowPos2i(6,6);glDrawPixels(2,2,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    const GLubyte draw_first[]={0,255,0,255,0,0,255,255};
    const GLubyte draw_second[]={255,0,0,255,255,255,0,255};
    read_row(6,6,draw_first,2);read_row(6,7,draw_second,2);

    enum { stride=8, span=32*stride+5 };
    GLubyte *mask=(GLubyte *)(guard+page-span);
    memset(mask,0,span);
    for(unsigned row=1;row<=32;++row)mask[row*stride+(row&1?0:4)]=(row&1)?8:4;
    glPixelStorei(GL_UNPACK_ROW_LENGTH,37);glPixelStorei(GL_UNPACK_SKIP_PIXELS,3);
    glPolygonStipple(mask);CHECK(glGetError()==GL_NO_ERROR);
    glPixelStorei(GL_PACK_ALIGNMENT,4);glPixelStorei(GL_PACK_ROW_LENGTH,37);
    glPixelStorei(GL_PACK_SKIP_ROWS,1);glPixelStorei(GL_PACK_SKIP_PIXELS,3);
    glPixelStorei(GL_PACK_LSB_FIRST,GL_TRUE);
    memset(mask,0xa0,span);glGetPolygonStipple(mask);CHECK(glGetError()==GL_NO_ERROR);
    CHECK(mask[0]==0xa0 && mask[stride+5]==0xa0);
    for(unsigned row=1;row<=32;++row){
        CHECK((mask[row*stride]&0xf8)==(row&1?8:0));
        CHECK((mask[row*stride+4]&7)==(row&1?0:4));
    }
    printf("stipple boundary bytes: %02x %02x %02x %02x\n",mask[stride],mask[stride+4],
           mask[32*stride],mask[32*stride+4]);
    /* Inspect, but don't assume, replacement of pre-existing bits in the
     * partial final byte. Native GL on Metal can leave an all-zero tail
     * intact while updating the preceding full bytes. */
    memset(mask,0xa5,span);glGetPolygonStipple(mask);CHECK(glGetError()==GL_NO_ERROR);
    printf("stipple seeded tail bytes: %02x %02x\n",mask[stride+4],mask[32*stride+4]);

    GLuint buffers[2]={0};glGenBuffers(2,buffers);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,buffers[0]);
    GLubyte seed[span+3];memset(seed,0xa5,sizeof(seed));
    glBufferData(GL_PIXEL_PACK_BUFFER,sizeof(seed),seed,GL_STATIC_DRAW);
    glGetPolygonStipple((void *)2);
    glGetBufferSubData(GL_PIXEL_PACK_BUFFER,0,sizeof(seed),seed);
    CHECK(glGetError()==GL_NO_ERROR && seed[0]==0xa5 && seed[1]==0xa5 &&
          seed[span+2]==0xa5 && !memcmp(seed+2,mask,span));
#ifdef VIM_COMPAT_RASTER_WIRE
    glGetPolygonStipple((void *)4);CHECK(glGetError()==GL_INVALID_OPERATION);
#endif
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
    glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);
    glPixelStorei(GL_PACK_ROW_LENGTH,0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
    const GLubyte pbo_pixels[]={255,0,0,255,0,255,255,255,255,0,255,255};
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,buffers[1]);
    glBufferData(GL_PIXEL_UNPACK_BUFFER,sizeof(pbo_pixels),pbo_pixels,GL_STATIC_DRAW);
    glWindowPos2i(1,13);glDrawPixels(2,1,GL_RGBA,GL_UNSIGNED_BYTE,(const void *)4);
    read_row(1,13,pbo_pixels+4,2);
    glWindowPos2i(4,13);glBitmap(1,1,0,0,0,0,NULL);read_row(4,13,pbo_pixels,1);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glDeleteBuffers(2,buffers);

    /* Bitmap fonts compile reusable display lists. Native GL must snapshot
     * the bitmap, and playback must advance the caller's raster position. */
    GLuint glyph=glGenLists(1);GLubyte glyph_bits=1;
    CHECK(glyph!=0);glNewList(glyph,GL_COMPILE);
    glBitmap(1,1,0,0,3,0,&glyph_bits);glEndList();glyph_bits=0;
    glWindowPos2i(9,9);glCallList(glyph);
    glGetFloatv(GL_CURRENT_RASTER_POSITION,position);
    CHECK(glGetError()==GL_NO_ERROR && position[0]==12 && position[1]==9);
    read_row(9,9,pbo_pixels,1);glDeleteLists(glyph,1);
    munmap(guard,2*page);
    glDeleteFramebuffersEXT(1,&fb);glDeleteRenderbuffersEXT(1,&rb);
}
int main(void)
{
#ifdef VIM_COMPAT_RASTER_WIRE
    const int attrs[]={0x56494d02,0x1000,0};unsigned char screen[16*16*4]={0};
    struct DGContext *c=DGCreateContext(attrs,NULL);
    if(!c || !DGMakeCurrent(c,screen,GL_UNSIGNED_BYTE,16,16))return 2;
    CHECK(c->connection->features2&DG_FEATURE2_COMPAT_RASTER);
#else
    const CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;
    CGLDestroyPixelFormat(pf);
#endif
    printf("compatibility raster renderer=%s\n",glGetString(GL_RENDERER));verify();
#ifdef VIM_COMPAT_RASTER_WIRE
    CHECK(!c->connection->dead);DGDestroyContext(c);
#else
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);
#endif
    printf("compatibility raster failures=%d\n",failures);return failures?1:0;
}
