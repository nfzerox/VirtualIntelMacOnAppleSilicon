/* Bitmap pack-buffer content, bounds and deleted-name lifetime on real CGL. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <string.h>
static unsigned failures;
static unsigned char stencil_pixels[64*3];
static int transfer_shift,transfer_offset,transfer_invert;
static void check(int good,const char *name)
{printf("%s %s\n",good?"PASS":"FAIL",name);fflush(stdout);failures+=!good;}
static void store(int lsb,int rows,int pixels,int stride)
{
    glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_PACK_ROW_LENGTH,0);
    glPixelStorei(GL_PACK_SKIP_ROWS,rows);glPixelStorei(GL_PACK_SKIP_PIXELS,pixels);
    glPixelStorei(GL_PACK_ROW_BYTES_APPLE,stride);glPixelStorei(GL_PACK_LSB_FIRST,lsb);
}
static void framebuffer(GLuint texture,GLuint stencil,GLuint *fbo)
{
    glGenFramebuffers(1,fbo);glBindFramebuffer(GL_FRAMEBUFFER,*fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,stencil);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_STENCIL_ATTACHMENT,GL_RENDERBUFFER,stencil);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"bitmap PBO framebuffer");
}
static void read_shape_error(GLuint name,int lsb,unsigned width,unsigned shift,const char *label,GLenum pending)
{
    unsigned char seed[64],expected[64],actual[64];memset(seed,0xa5,sizeof(seed));memcpy(expected,seed,sizeof(seed));
    GLint access=0;glGetBufferParameteriv(GL_PIXEL_PACK_BUFFER,GL_BUFFER_ACCESS,&access);
    unsigned stride=(width+shift+7)/8+3;
    glBufferSubData(GL_PIXEL_PACK_BUFFER,0,sizeof(seed),seed);store(lsb,1,(int)shift,(int)stride);
    for(unsigned row=0;row<3;++row)for(unsigned x=0;x<width;++x){unsigned bit=x+shift;
        unsigned mask=1u<<(lsb?bit%8:7-bit%8),value=stencil_pixels[row*64+x];
        value=transfer_shift<0?value>>-transfer_shift:value<<transfer_shift;
        value=((value+(unsigned)transfer_offset)^(unsigned)transfer_invert)&1u;
        unsigned char *p=expected+3+stride+row*stride+bit/8;
        *p=(unsigned char)((*p&~mask)|(value?mask:0));}
    if(pending)glEnable(0xffffffffu);
    glReadPixels(0,0,(GLsizei)width,3,GL_STENCIL_INDEX,GL_BITMAP,(void *)3);GLenum error=glGetError();
    memset(actual,0,sizeof(actual));glGetBufferSubData(GL_PIXEL_PACK_BUFFER,0,sizeof(actual),actual);
    char description[160];snprintf(description,sizeof(description),"%s width=%u shift=%u lsb=%d",label,width,shift,lsb);
    int good=error==pending&&!glGetError()&&!memcmp(expected,actual,sizeof(actual));
    if(!good){printf("%s error=%x bytes=",description,error);for(unsigned i=0;i<32;++i)printf("%02x",actual[i]);puts("");}
    check(good,description);
    GLint binding=0,size=0;glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&binding);
    glGetBufferParameteriv(GL_PIXEL_PACK_BUFFER,GL_BUFFER_SIZE,&size);
    GLint actual_stride=0,actual_skip=0,actual_access=0;glGetIntegerv(GL_PACK_ROW_BYTES_APPLE,&actual_stride);
    glGetIntegerv(GL_PACK_SKIP_PIXELS,&actual_skip);
    glGetBufferParameteriv(GL_PIXEL_PACK_BUFFER,GL_BUFFER_ACCESS,&actual_access);
    check((GLuint)binding==name&&size==64&&actual_access==access&&actual_stride==(GLint)stride&&actual_skip==(GLint)shift&&!glGetError(),
        "bitmap read preserves PBO binding, size, access and pack state");
}
static void read_shape(GLuint name,int lsb,unsigned width,unsigned shift,const char *label)
{read_shape_error(name,lsb,width,shift,label,0);}
static void shapes(GLuint name,const char *label)
{
    const unsigned widths[]={1,7,8,10,16,17,31,32,33};
    for(unsigned w=0;w<sizeof(widths)/sizeof(widths[0]);++w)
        for(unsigned shift=0;shift<8;++shift)for(int lsb=0;lsb<2;++lsb)
            read_shape(name,lsb,widths[w],shift,label);
}
static void transfers(GLuint name)
{
    transfer_shift=-1;glPixelTransferi(GL_INDEX_SHIFT,-1);
    for(int lsb=0;lsb<2;++lsb)read_shape(name,lsb,33,7,"bitmap PBO native index shift");
    transfer_shift=0;glPixelTransferi(GL_INDEX_SHIFT,0);
    transfer_offset=3;glPixelTransferi(GL_INDEX_OFFSET,3);
    for(int lsb=0;lsb<2;++lsb)read_shape(name,lsb,17,3,"bitmap PBO native index offset");
    transfer_offset=0;glPixelTransferi(GL_INDEX_OFFSET,0);
    GLuint table[]={1,0};glPixelMapuiv(GL_PIXEL_MAP_S_TO_S,2,table);
    transfer_invert=1;glPixelTransferi(GL_MAP_STENCIL,GL_TRUE);
    for(int lsb=0;lsb<2;++lsb)read_shape(name,lsb,17,5,"bitmap PBO native stencil map");
    GLboolean enabled=0;glGetBooleanv(GL_MAP_STENCIL,&enabled);
    check(enabled&&!glGetError(),"bitmap PBO preserves native stencil transfer state");
    transfer_invert=0;glPixelTransferi(GL_MAP_STENCIL,GL_FALSE);
}
int main(void)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj parent=NULL,child=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attrs,&pf,&count)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&parent)||CGLCreateContext(pf,parent,&child))return 2;
    CGLDestroyPixelFormat(pf);CGLSetCurrentContext(parent);
    printf("bitmap PBO renderer=%s version=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    GLuint texture=0,stencil=0,fbo=0,child_fbo=0,buffer=0;
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,64,3,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenRenderbuffers(1,&stencil);glBindRenderbuffer(GL_RENDERBUFFER,stencil);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8_EXT,64,3);framebuffer(texture,stencil,&fbo);
    glClearStencil(1);glClear(GL_STENCIL_BUFFER_BIT);
    memset(stencil_pixels,1,sizeof(stencil_pixels));
    glGenBuffers(1,&buffer);glBindBuffer(GL_PIXEL_PACK_BUFFER,buffer);
    unsigned char seed[64],actual[64];memset(seed,0xa5,sizeof(seed));
    glBufferData(GL_PIXEL_PACK_BUFFER,sizeof(seed),seed,GL_DYNAMIC_DRAW);
    read_shape(buffer,0,10,3,"named PBO bitmap pixels");read_shape(buffer,1,10,3,"named PBO bitmap pixels");
    store(0,0,0,0);glBufferData(GL_PIXEL_PACK_BUFFER,2,seed,GL_DYNAMIC_DRAW);
    glReadPixels(0,0,10,1,GL_STENCIL_INDEX,GL_BITMAP,NULL);GLenum error=glGetError();
    glGetBufferSubData(GL_PIXEL_PACK_BUFFER,0,2,actual);
    printf("two-byte bitmap error=%x bytes=%02x,%02x\n",error,actual[0],actual[1]);
    check(!error&&!glGetError()&&actual[0]==255&&(actual[1]&0xc0)==0xc0,"one-row bitmap fits two-byte PBO");
    glBufferData(GL_PIXEL_PACK_BUFFER,sizeof(seed),seed,GL_DYNAMIC_DRAW);
    glReadPixels(0,0,10,3,GL_STENCIL_INDEX,GL_BITMAP,(void *)64);
    check(glGetError()==GL_INVALID_OPERATION,"bitmap PBO overrun rejected");
    glGetBufferSubData(GL_PIXEL_PACK_BUFFER,0,sizeof(actual),actual);
    check(!glGetError()&&!memcmp(actual,seed,sizeof(seed)),"overrun leaves PBO unchanged");
    void *mapped=glMapBuffer(GL_PIXEL_PACK_BUFFER,GL_READ_WRITE);
    check(mapped&&!glGetError(),"bitmap PBO mapping control");
    if(mapped){glReadPixels(0,0,10,1,GL_STENCIL_INDEX,GL_BITMAP,NULL);
        check(glGetError()==GL_INVALID_OPERATION&&!memcmp(mapped,seed,sizeof(seed)),"mapped bitmap PBO rejected without mutation");glUnmapBuffer(GL_PIXEL_PACK_BUFFER);}
    glEnable(GL_SCISSOR_TEST);
    for(unsigned row=0;row<3;++row)for(unsigned x=0;x<64;++x){
        stencil_pixels[row*64+x]=(unsigned char)((x+row*7)*13);
        glScissor((GLint)x,(GLint)row,1,1);glClearStencil(stencil_pixels[row*64+x]);glClear(GL_STENCIL_BUFFER_BIT);}
    glDisable(GL_SCISSOR_TEST);check(!glGetError(),"varying native stencil source");
    void *readonly=glMapBuffer(GL_PIXEL_PACK_BUFFER,GL_READ_ONLY);
    check(readonly&&!glGetError(),"read-only mapping history control");
    if(readonly)glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    shapes(buffer,"named PBO patterned bitmap");
    transfers(buffer);
    read_shape_error(buffer,0,17,5,"named PBO retains prior error",GL_INVALID_ENUM);
    GLuint incomplete=0;glGenFramebuffers(1,&incomplete);glBindFramebuffer(GL_FRAMEBUFFER,incomplete);
    glBufferSubData(GL_PIXEL_PACK_BUFFER,0,sizeof(seed),seed);store(0,0,0,0);
    glReadPixels(0,0,10,3,GL_STENCIL_INDEX,GL_BITMAP,NULL);
    GLenum invalid=glGetError();glGetBufferSubData(GL_PIXEL_PACK_BUFFER,0,sizeof(actual),actual);
    check(invalid==GL_INVALID_FRAMEBUFFER_OPERATION_EXT&&!glGetError()&&!memcmp(actual,seed,sizeof(actual)),
        "invalid source framebuffer leaves bitmap PBO unchanged");
    glBindFramebuffer(GL_FRAMEBUFFER,fbo);glDeleteFramebuffers(1,&incomplete);
    CGLSetCurrentContext(child);framebuffer(texture,stencil,&child_fbo);glBindBuffer(GL_PIXEL_PACK_BUFFER,buffer);
    CGLSetCurrentContext(parent);glDeleteBuffers(1,&buffer);CGLSetCurrentContext(child);
    check(!glIsBuffer(buffer)&&!glGetError(),"deleted name really remains bound but is not live");
    shapes(buffer,"deleted-name PBO patterned bitmap");
    transfers(buffer);
    read_shape_error(buffer,1,17,5,"deleted-name PBO retains prior error",GL_INVALID_ENUM);
    GLint depth=0,maximum=0;glGetIntegerv(GL_CLIENT_ATTRIB_STACK_DEPTH,&depth);
    glGetIntegerv(GL_MAX_CLIENT_ATTRIB_STACK_DEPTH,&maximum);
    for(GLint i=depth;i<maximum;++i)glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    check(!glGetError(),"fill client stack without retaining pixel binding");
    read_shape(buffer,1,33,7,"deleted-name PBO with full client stack");
    for(GLint i=depth;i<maximum;++i)glPopClientAttrib();
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glDeleteFramebuffers(1,&child_fbo);CGLSetCurrentContext(parent);
    glDeleteFramebuffers(1,&fbo);glDeleteRenderbuffers(1,&stencil);glDeleteTextures(1,&texture);
    CGLSetCurrentContext(NULL);CGLDestroyContext(child);CGLDestroyContext(parent);
    printf("bitmap-pbo failures=%u\n",failures);return failures?1:0;
}
