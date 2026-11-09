/* The same texture-readback fixture can run with real CGL or the wire client.
 * It compares padding and skipped bytes as well as the image values. */
#ifdef VIM_READBACK_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;} } while(0)

static void pack(GLint row,GLint skiprow,GLint skipx,GLint image,GLint skipimage)
{
    glPixelStorei(GL_PACK_ROW_LENGTH,row);
    glPixelStorei(GL_PACK_SKIP_ROWS,skiprow);
    glPixelStorei(GL_PACK_SKIP_PIXELS,skipx);
    glPixelStorei(GL_PACK_IMAGE_HEIGHT,image);
    glPixelStorei(GL_PACK_SKIP_IMAGES,skipimage);
}

static void texture(GLenum target,GLenum image_target,GLint level)
{
    unsigned char input[32],want[256],out[256];
    for(unsigned i=0;i<sizeof(input);++i)input[i]=(unsigned char)(i+17);
    GLuint name=0;glGenTextures(1,&name);glBindTexture(target,name);
    if(target==GL_TEXTURE_1D)
        glTexImage1D(target,level,GL_RGBA8,2,0,GL_RGBA,GL_UNSIGNED_BYTE,input);
    else if(target==GL_TEXTURE_3D || target==GL_TEXTURE_2D_ARRAY_EXT)
        glTexImage3D(target,level,GL_RGBA8,2,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,input);
    else glTexImage2D(image_target,level,GL_RGBA8,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,input);
    CHECK(glGetError()==GL_NO_ERROR);
    memset(out,0xa5,sizeof(out));memset(want,0xa5,sizeof(want));
    pack(4,1,1,4,1);
    unsigned h=target==GL_TEXTURE_1D?1:2,d=(target==GL_TEXTURE_3D || target==GL_TEXTURE_2D_ARRAY_EXT)?2:1;
    for(unsigned z=0;z<d;++z)for(unsigned y=0;y<h;++y)
        memcpy(want+4+16+
            (d==2?(z+1)*64:0)+y*16,input+(z*h+y)*8,8);
    glGetTexImage(image_target,level,GL_RGBA,GL_UNSIGNED_BYTE,out);
    GLenum e=glGetError();
    if(e || memcmp(want,out,sizeof(want))){
        fprintf(stderr,"pack target=%x level=%d error=%x actual writes:",image_target,level,e);
        for(unsigned i=0;i<sizeof(out);++i)if(out[i]!=0xa5)fprintf(stderr," %u=%u",i,out[i]);
        fputc('\n',stderr);
    }
    CHECK(e==GL_NO_ERROR && !memcmp(want,out,sizeof(want)));
    pack(0,0,0,0,0);
    glDeleteTextures(1,&name);
}

static void pbo(void)
{
    GLuint tex=0,b=0;
    const unsigned char input[]={5,9,13,25,39,47,55,71};
    unsigned char out[32],expected[32];
    memset(expected,0xa5,sizeof(expected));
    glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,2,1,0,GL_RGBA,GL_UNSIGNED_BYTE,input);
    glGenBuffers(1,&b);glBindBuffer(GL_PIXEL_PACK_BUFFER,b);
    glBufferData(GL_PIXEL_PACK_BUFFER,sizeof(expected),expected,GL_STATIC_READ);
    glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,(void *)4);
    glGetBufferSubData(GL_PIXEL_PACK_BUFFER,0,sizeof(out),out);
    memcpy(expected+4,input,sizeof(input));
    CHECK(glGetError()==GL_NO_ERROR && !memcmp(out,expected,sizeof(out)));
    glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,(void *)28);
    CHECK(glGetError()==GL_INVALID_OPERATION);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glDeleteBuffers(1,&b);glDeleteTextures(1,&tex);
}

static void compressed(void)
{
    GLuint tex=0;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
    const unsigned char bc1[]={0x00,0xf8,0xe0,0x07,0,0,0,0};
    glCompressedTexImage2D(GL_TEXTURE_2D,0,GL_COMPRESSED_RGB_S3TC_DXT1_EXT,4,4,0,sizeof(bc1),bc1);
    CHECK(glGetError()==GL_NO_ERROR);
    unsigned char out[16];memset(out,0xa5,sizeof(out));
    pack(16,1,2,4,1);
    glGetCompressedTexImage(GL_TEXTURE_2D,0,out);
    CHECK(glGetError()==GL_NO_ERROR && !memcmp(out,bc1,sizeof(bc1)) && out[8]==0xa5);
    pack(0,0,0,0,0);
    GLuint b=0;glGenBuffers(1,&b);glBindBuffer(GL_PIXEL_PACK_BUFFER,b);
    memset(out,0xa5,sizeof(out));
    glBufferData(GL_PIXEL_PACK_BUFFER,sizeof(out),out,GL_STATIC_READ);
    glGetCompressedTexImage(GL_TEXTURE_2D,0,(void *)0);
    glGetBufferSubData(GL_PIXEL_PACK_BUFFER,0,sizeof(out),out);
    CHECK(glGetError()==GL_NO_ERROR && !memcmp(out,bc1,sizeof(bc1)) && out[8]==0xa5);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glDeleteBuffers(1,&b);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    out[0]=0xa5;glGetCompressedTexImage(GL_TEXTURE_2D,0,out);
    CHECK(glGetError()==GL_INVALID_OPERATION && out[0]==0xa5);
    glDeleteTextures(1,&tex);
}

static void sparse_rows(void)
{
    size_t page=(size_t)getpagesize();
    unsigned char *dst=mmap(NULL,3*page,PROT_NONE,MAP_ANON|MAP_PRIVATE,-1,0);
    if(dst==MAP_FAILED){CHECK(0);return;}
    if(mprotect(dst,page,PROT_READ|PROT_WRITE) || mprotect(dst+2*page,page,PROT_READ|PROT_WRITE)){
        CHECK(0);munmap(dst,3*page);return;
    }
    /* 3-byte RGB rows stress the tight transfer's native pack alignment. */
    const unsigned char data[]={1,2,3,4,5,6};
    GLuint name=0;glGenTextures(1,&name);glBindTexture(GL_TEXTURE_2D,name);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGB8,1,2,0,GL_RGB,GL_UNSIGNED_BYTE,data);
    glPixelStorei(GL_UNPACK_ALIGNMENT,4);
    glPixelStorei(GL_PACK_ROW_LENGTH,(GLint)(2*page/3));
    glGetTexImage(GL_TEXTURE_2D,0,GL_RGB,GL_UNSIGNED_BYTE,dst);
    CHECK(glGetError()==GL_NO_ERROR && !memcmp(dst,data,3) && !memcmp(dst+2*page,data+3,3));
    GLint row=0;glGetIntegerv(GL_PACK_ROW_LENGTH,&row);
    CHECK(glGetError()==GL_NO_ERROR && row==(GLint)(2*page/3));
    glPixelStorei(GL_PACK_ROW_LENGTH,0);
    unsigned char odd[9];memset(odd,0xa5,sizeof(odd));
    glPixelStorei(GL_PACK_ROW_BYTES_APPLE,5);
    glGetTexImage(GL_TEXTURE_2D,0,GL_RGB,GL_UNSIGNED_BYTE,odd);
    CHECK(glGetError()==GL_NO_ERROR && !memcmp(odd,data,3) && !memcmp(odd+5,data+3,3) &&
        odd[3]==0xa5 && odd[4]==0xa5 && odd[8]==0xa5);
    glPixelStorei(GL_PACK_ROW_BYTES_APPLE,0);
#ifdef VIM_READBACK_WIRE
    /* GLI keeps APPLE row bytes as guest state, not glPixelStorei. */
    memset(odd,0xa5,sizeof(odd));
    CHECK(DGReadTextureWithRowBytes(GL_TEXTURE_2D,0,GL_RGB,GL_UNSIGNED_BYTE,odd,5));
    CHECK(glGetError()==GL_NO_ERROR && !memcmp(odd,data,3) && !memcmp(odd+5,data+3,3) &&
        odd[3]==0xa5 && odd[4]==0xa5 && odd[8]==0xa5);
#endif
    glDeleteTextures(1,&name);
    munmap(dst,3*page);
}

static void large_texture(void)
{
    const unsigned width=4097,height=4097;
    const size_t stride=(width+2)*4u,offset=stride+4,bytes=(height+2)*stride;
    unsigned char *out=malloc(bytes);
    if(!out){CHECK(0);return;}
    memset(out,0xa5,bytes);
    GLuint tex=0,fbo=0;
    glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,tex,0);
    CHECK(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);
    const unsigned char sample[]={0,255,17,255},red[]={255,0,0,255};
    /* A chunk boundary splits this row; the next row must still land at
     * the requested padded stride rather than immediately after its bytes. */
    const unsigned row=(4u*1024u*1024u)/(width*4u);
    const unsigned column=(4u*1024u*1024u)%(width*4u)/4;
    glTexSubImage2D(GL_TEXTURE_2D,0,column,row,1,1,GL_RGBA,GL_UNSIGNED_BYTE,sample);
    CHECK(glGetError()==GL_NO_ERROR);
    pack(width+2,1,1,0,0);
    glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,out);
    CHECK(glGetError()==GL_NO_ERROR);
    CHECK(!memcmp(out+offset+row*stride+column*4u,sample,4));
    unsigned mismatches=0;
    for(unsigned y=0;y<height;++y){
        const unsigned char *p=out+offset+y*stride;
        if(p[-1]!=0xa5 || p[width*4u]!=0xa5 || memcmp(p,red,4) ||
           memcmp(p+(width-1)*4u,red,4))++mismatches;
    }
    CHECK(!mismatches && out[0]==0xa5 && out[bytes-1]==0xa5);
#ifdef VIM_READBACK_WIRE
    /* Negotiation must still keep a large transfer away from an old peer. */
    struct DGContext *c=dg_current;
    CHECK(c->connection->features&DG_FEATURE_TEXTURE_READ_CHUNKS);
    struct DGPacket stale=DGPacketNew(c,DG_TEXTURE_READ);
    unsigned char unused=0;
    stale.message.flags=DG_OUTPUT_ONLY;stale.message.args[0]=DG_TEXTURE_READ_CHUNK;
    DGPacketPointer(&stale,6,&unused,1,1);
    struct DGReply reply=DGSubmit(c,&stale,1);
    CHECK(reply.error==GL_INVALID_OPERATION && glGetError()==GL_INVALID_OPERATION);
    DGPacketFree(&stale);
    c->connection->features&=~DG_FEATURE_TEXTURE_READ_CHUNKS;
    out[offset]=0xa5;glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,out);
    CHECK(glGetError()==GL_OUT_OF_MEMORY && out[offset]==0xa5);
    c->connection->features|=DG_FEATURE_TEXTURE_READ_CHUNKS;
#endif
    pack(0,0,0,0,0);
    glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&tex);free(out);
}

static void large_compressed(void)
{
    const unsigned width=8192,height=8196;
    const GLenum encoding=GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    const unsigned char first[]={255,0,0,0,0,0,0,0,0,0xf8,0xe0,7,0,0,0,0};
    const unsigned char last[]={0,255,0,0,0,0,0,0,0xe0,7,0,0xf8,0,0,0,0};
    const size_t bytes=(size_t)(width/4)*(height/4)*sizeof(first);
    GLuint tex=0;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
    glTexImage2D(GL_TEXTURE_2D,0,encoding,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    GLint compressed=0,reported=0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_COMPRESSED,&compressed);
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_COMPRESSED_IMAGE_SIZE,&reported);
    GLenum error=glGetError();
    CHECK(error==GL_NO_ERROR && compressed==GL_TRUE && reported==(GLint)bytes);
    if(error || reported!=(GLint)bytes){glDeleteTextures(1,&tex);return;}
    glCompressedTexSubImage2D(GL_TEXTURE_2D,0,0,0,4,4,encoding,sizeof(first),first);
    glCompressedTexSubImage2D(GL_TEXTURE_2D,0,width-4,height-4,4,4,encoding,sizeof(last),last);
    CHECK(glGetError()==GL_NO_ERROR);
    unsigned char *out=malloc(bytes+1);
    if(!out){CHECK(0);glDeleteTextures(1,&tex);return;}
    memset(out,0xa5,bytes+1);
    glGetCompressedTexImage(GL_TEXTURE_2D,0,out);
    CHECK(glGetError()==GL_NO_ERROR && !memcmp(out,first,sizeof(first)) &&
        !memcmp(out+bytes-sizeof(last),last,sizeof(last)) && out[bytes]==0xa5);
    glDeleteTextures(1,&tex);free(out);
}

int main(int argc,char **argv)
{
    /* The period batch explicitly chooses compatibility contexts on systems
     * before profiles existed. Don't label a second legacy context as core. */
#ifdef VIM_PERIOD_COMPAT_ONLY
    const int profiles=1;
#else
    const int profiles=2;
#endif
    for(int core=0;core<profiles;++core){
#ifdef VIM_READBACK_WIRE
    (void)argc;(void)argv;
    unsigned char screen[16*16*4]={0};
    const int attrs[]={0x56494d02,core?0x3200:0x1000,0};
    struct DGContext *c=DGCreateContext(attrs,NULL);
    if(!c || !DGMakeCurrent(c,screen,GL_UNSIGNED_BYTE,16,16))return 2;
#else
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)(core?kCGLOGLPVersion_3_2_Core:kCGLOGLPVersion_Legacy),0,0,0};
    if(argc==2){attrs[5]=kCGLPFARendererID;attrs[6]=(CGLPixelFormatAttribute)strtoul(argv[1],NULL,16);}
    CGLPixelFormatObj format=NULL;GLint n=0;CGLContextObj c=NULL;
    if(CGLChoosePixelFormat(attrs,&format,&n)||!format||CGLCreateContext(format,NULL,&c)||
        CGLSetCurrentContext(c))return 2;
    CGLDestroyPixelFormat(format);
    if(argc==2 && !strstr((const char *)glGetString(GL_VENDOR),"Intel"))return 77;
#endif
    printf("texture readback renderer=%s core=%d\n",glGetString(GL_RENDERER),core);
    texture(GL_TEXTURE_1D,GL_TEXTURE_1D,1);
    texture(GL_TEXTURE_2D,GL_TEXTURE_2D,1);
    texture(GL_TEXTURE_3D,GL_TEXTURE_3D,0);
    if(core){
        texture(GL_TEXTURE_1D_ARRAY_EXT,GL_TEXTURE_1D_ARRAY_EXT,1);
        texture(GL_TEXTURE_2D_ARRAY_EXT,GL_TEXTURE_2D_ARRAY_EXT,0);
    }
    texture(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_CUBE_MAP_POSITIVE_Y,1);
    texture(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_RECTANGLE_ARB,0);
    pbo();compressed();sparse_rows();
    if(core){large_texture();large_compressed();}
#ifdef VIM_READBACK_WIRE
    CHECK(!c->connection->dead);DGDestroyContext(c);
#else
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);
#endif
    }
    printf("texture readback failures=%d\n",failures);
    return failures?1:0;
}
