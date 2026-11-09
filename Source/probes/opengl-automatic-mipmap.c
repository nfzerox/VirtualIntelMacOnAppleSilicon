/* Public compatibility-GL automatic-generation controls. No adapter/injection.
 * Constant images give an algorithm-independent oracle; patterned images are
 * printed separately because SGIS does not prescribe a particular filter.
 * Run the identical executable on the Intel reference and virtual guest. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef VIM_AUTOMATIC_NATIVE_CONTROL
#include "../PassthroughOpenGL/passthrough-opengl-mipmap-host.h"
#include "../PassthroughOpenGL/passthrough-opengl-automatic-mipmap-host.h"
static int adapted;
static GLenum adapter_error;
static struct DGMipmapExecution execution;
static GLenum control_error(void)
{GLenum error=glGetError();if(adapter_error){error=adapter_error;adapter_error=0;}return error;}
#define glGetError control_error
#endif

static unsigned checks, failures, skipped;
static int dense,clipped_max,non_pot,copy_auto_off;
static GLenum upload_list_mode;
static GLuint begin_upload(void)
{
    if(!upload_list_mode)return 0;
    GLint current=0;glGetIntegerv(GL_LIST_INDEX,&current);if(current)return 0;
    GLuint list=glGenLists(1);glNewList(list,upload_list_mode);return list;
}
static void end_upload(GLuint list)
{if(list){glEndList();glCallList(list);glDeleteLists(list,1);}}
static const unsigned char original[4]={17,71,131,191};
static const unsigned char updated[4]={201,101,51,171};
static const unsigned char sentinel[4]={29,89,149,209};
static void check(int value,const char *what)
{++checks;failures+=!value;printf("%s %s\n",value?"PASS":"FAIL",what);fflush(stdout);}
static int extension(const char *name)
{
    const char *all=(const char *)glGetString(GL_EXTENSIONS);size_t n=strlen(name);
    for(const char *p=all;p&&(p=strstr(p,name));p+=n)
        if((p==all||p[-1]==' ')&&(!p[n]||p[n]==' '))return 1;
    return 0;
}
static unsigned count(GLenum target,unsigned size)
{return size*(target==GL_TEXTURE_1D?1:size)*(target==GL_TEXTURE_3D?size:1);}
static void fill(unsigned char *pixels,unsigned n,const unsigned char *seed)
{for(unsigned i=0;i<n*4;++i)pixels[i]=seed[i%4];}
static void define(GLenum target,GLint level,GLint format,int size,const void *pixels)
{
    GLuint list=begin_upload();
#ifdef VIM_AUTOMATIC_NATIVE_CONTROL
    struct DGAutomaticMipmapScope scope=DGBeginAutomaticMipmap(target,level,adapted,0,0,&adapter_error,0,1);
#endif
    if(target==GL_TEXTURE_1D)glTexImage1D(target,level,format,size,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    else if(target==GL_TEXTURE_2D)glTexImage2D(target,level,format,size,size,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    else glTexImage3D(target,level,format,size,size,size,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
#ifdef VIM_AUTOMATIC_NATIVE_CONTROL
    DGEndAutomaticMipmap(&scope,&execution,&adapter_error);
#endif
    end_upload(list);
}
static void update_at(GLenum target,GLint level,int x,int size,const void *pixels)
{
    GLuint list=begin_upload();
#ifdef VIM_AUTOMATIC_NATIVE_CONTROL
    struct DGAutomaticMipmapScope scope=DGBeginAutomaticMipmap(target,level,adapted,0,0,&adapter_error,0,1);
#endif
    if(target==GL_TEXTURE_1D)glTexSubImage1D(target,level,x,size,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    else if(target==GL_TEXTURE_2D)glTexSubImage2D(target,level,x,0,size,size,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    else glTexSubImage3D(target,level,x,0,0,size,size,size,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
#ifdef VIM_AUTOMATIC_NATIVE_CONTROL
    DGEndAutomaticMipmap(&scope,&execution,&adapter_error);
#endif
    end_upload(list);
}
static void update(GLenum target,GLint level,int size,const void *pixels)
{update_at(target,level,0,size,pixels);}
static void copy_update(int subimage)
{
    GLuint list=begin_upload();
#ifdef VIM_AUTOMATIC_NATIVE_CONTROL
    struct DGAutomaticMipmapScope scope=DGBeginAutomaticMipmap(GL_TEXTURE_2D,2,adapted,0,0,&adapter_error,1,1);
#endif
    if(subimage)glCopyTexSubImage2D(GL_TEXTURE_2D,2,0,0,0,0,4,4);
    else glCopyTexImage2D(GL_TEXTURE_2D,2,GL_RGBA8,0,0,4,4,0);
#ifdef VIM_AUTOMATIC_NATIVE_CONTROL
    DGEndAutomaticMipmap(&scope,&execution,&adapter_error);
#endif
    end_upload(list);
}
static int matches(GLenum target,int level,int size,GLint format,const unsigned char *seed)
{
    unsigned char out[16384];GLint w=0,h=0,d=0;
    glGetTexLevelParameteriv(target,level,GL_TEXTURE_WIDTH,&w);
    glGetTexLevelParameteriv(target,level,GL_TEXTURE_HEIGHT,&h);
    glGetTexLevelParameteriv(target,level,GL_TEXTURE_DEPTH,&d);
    if(w!=size||h!=(target==GL_TEXTURE_1D?1:size)||d!=(target==GL_TEXTURE_3D?size:1)){
        printf("geometry level=%d actual=%d,%d,%d expected=%d\n",level,w,h,d,size);return 0;
    }
    memset(out,0,sizeof(out));glGetTexImage(target,level,GL_RGBA,GL_UNSIGNED_BYTE,out);
    GLenum error=glGetError();if(error){printf("readback error=%x\n",error);return 0;}
    for(unsigned i=0;i<count(target,(unsigned)size)*4;++i){
        unsigned expected=(i%4==3&&format==GL_RGB8)?255:seed[i%4];
        int delta=(int)out[i]-(int)expected;
        if(delta < -1||delta > 1){printf("pixel level=%d byte=%u actual=%u expected=%u\n",level,i,out[i],expected);return 0;}
    }return 1;
}
static void run(GLenum target,GLint format,int base,int named)
{
    GLuint texture=0;unsigned char pixels[65536];GLint value=0;
    int size=non_pot?6:4,child_size=size/2;
    if(named)glGenTextures(1,&texture);glBindTexture(target,texture);
    printf("case target=%x format=%x base=%d named=%d dense=%d size=%d\n",target,format,base,named,dense,size);fflush(stdout);
    /* Object zero may have survived an earlier case; reset explicitly. */
    glTexParameteri(target,GL_GENERATE_MIPMAP,GL_FALSE);
    glTexParameteri(target,GL_TEXTURE_BASE_LEVEL,base);
    GLint maximum=clipped_max?base:base+2;
    glTexParameteri(target,GL_TEXTURE_MAX_LEVEL,maximum);
    fill(pixels,count(target,1),sentinel);define(target,base+3,format,1,pixels);
    if(base){
        if(dense)for(int level=0;level<base;++level){
            unsigned lower=(unsigned)size<<(base-level);fill(pixels,count(target,lower),sentinel);
            define(target,level,format,(int)lower,pixels);
        }
        else {fill(pixels,count(target,2),sentinel);define(target,base-1,format,2,pixels);}
    }
    fill(pixels,count(target,(unsigned)size),original);define(target,base,format,size,pixels);
    fill(pixels,count(target,(unsigned)child_size),sentinel);define(target,base+1,format,child_size,pixels);
    check(!glGetError(),"source and sentinel levels accepted");
    glTexParameteri(target,GL_GENERATE_MIPMAP,GL_TRUE);
    check(matches(target,base+1,child_size,format,sentinel),"enabling alone does not replace existing child");
    fill(pixels,count(target,(unsigned)size),original);define(target,base,format,size,pixels);
    check(!glGetError(),"TexImage automatic update accepted");
    check(matches(target,base,size,format,original),"TexImage preserves base pixels");
    check(matches(target,base+1,child_size,format,original),"TexImage derives first child");
    check(matches(target,base+2,1,format,original),"TexImage derives last child");
    check(matches(target,base+3,1,format,sentinel),"automatic update preserves level beyond derived pyramid");
    if(base)check(matches(target,base-1,dense?size*2:2,format,sentinel),"automatic update preserves lower level");
    fill(pixels,count(target,(unsigned)size),updated);update(target,base,size,pixels);
    check(!glGetError(),"TexSubImage automatic update accepted");
    check(matches(target,base+1,child_size,format,updated),"TexSubImage refreshes children");
    glTexParameteri(target,GL_GENERATE_MIPMAP,GL_FALSE);
    fill(pixels,count(target,(unsigned)size),original);update(target,base,size,pixels);
    check(matches(target,base+1,child_size,format,updated),"disabled generation preserves children");
    glTexParameteri(target,GL_GENERATE_MIPMAP,GL_TRUE);
    fill(pixels,count(target,(unsigned)child_size),sentinel);update(target,base+1,child_size,pixels);
    check(matches(target,base+1,child_size,format,sentinel),"nonbase modification is retained");
    check(matches(target,base+2,1,format,updated),"nonbase modification does not regenerate descendants");
    glGetTexParameteriv(target,GL_GENERATE_MIPMAP,&value);check(value==GL_TRUE,"automatic state retained");
    glGetTexParameteriv(target,GL_TEXTURE_BASE_LEVEL,&value);check(value==base,"base state retained");
    glGetTexParameteriv(target,GL_TEXTURE_MAX_LEVEL,&value);check(value==maximum,"max state retained");
    check(!glGetError(),"case leaves no GL error");
    glTexParameteri(target,GL_GENERATE_MIPMAP,GL_FALSE);
    if(named)glDeleteTextures(1,&texture);
}
static void lower_controls(void)
{
    const GLenum targets[]={GL_TEXTURE_1D,GL_TEXTURE_2D,GL_TEXTURE_3D};
    const unsigned char modified[]={227,157,87,117};
    unsigned char pixels[16384];
    for(unsigned t=0;t<3;++t)for(int named=0;named<2;++named)
    for(int lower=0;lower<2;++lower)for(int subimage=0;subimage<2;++subimage){
        GLenum target=targets[t];GLuint texture=0;GLint value=0;
        if(named)glGenTextures(1,&texture);glBindTexture(target,texture);
        glTexParameteri(target,GL_GENERATE_MIPMAP,GL_FALSE);
        glTexParameteri(target,GL_TEXTURE_BASE_LEVEL,2);glTexParameteri(target,GL_TEXTURE_MAX_LEVEL,4);
        for(int level=0;level<5;++level){int size=16>>level;
            fill(pixels,count(target,(unsigned)size),level<2?sentinel:level==2?original:updated);
            define(target,level,GL_RGBA8,size,pixels);}
        glTexParameteri(target,GL_GENERATE_MIPMAP,GL_TRUE);
        check(!glGetError(),"lower-update dense source accepted");
        int size=16>>lower;fill(pixels,count(target,(unsigned)size),modified);
        printf("lower target=%x named=%d level=%d subimage=%d\n",target,named,lower,subimage);fflush(stdout);
        if(subimage)update(target,lower,size,pixels);else define(target,lower,GL_RGBA8,size,pixels);
        check(!glGetError(),"lower-level modification accepted");
        check(matches(target,lower,size,GL_RGBA8,modified),"modified lower level contains the new pixels");
        check(matches(target,2,4,GL_RGBA8,original),"lower-level modification preserves the selected base");
        check(matches(target,3,2,GL_RGBA8,updated),"lower-level modification does not regenerate first child");
        check(matches(target,4,1,GL_RGBA8,updated),"lower-level modification does not regenerate last child");
        glGetTexParameteriv(target,GL_GENERATE_MIPMAP,&value);check(value==GL_TRUE,"lower update retains automatic parameter");
        glGetTexParameteriv(target,GL_TEXTURE_BASE_LEVEL,&value);check(value==2,"lower update retains selected base");
        check(!glGetError(),"lower update leaves no error");
        glTexParameteri(target,GL_GENERATE_MIPMAP,GL_FALSE);if(named)glDeleteTextures(1,&texture);
    }
}
static void shared_lower(CGLContextObj current,int variant,int compiled)
{
    GLuint texture=0,list=0;GLint value=0;unsigned char pixels[1024];
    const unsigned char modified[]={227,157,87,117};
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexParameteri(GL_TEXTURE_2D,GL_GENERATE_MIPMAP,GL_FALSE);
    for(int level=0;level<5;++level){int size=16>>level;
        fill(pixels,(unsigned)(size*size),level<2?sentinel:level==2?original:updated);
        define(GL_TEXTURE_2D,level,GL_RGBA8,size,pixels);}
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,4);
    if(compiled){list=glGenLists(1);glNewList(list,GL_COMPILE);}
    GLint integer=2;GLfloat floating=2;
    if(variant==0)glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,integer);
    else if(variant==1)glTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,&integer);
    else if(variant==2)glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,floating);
    else glTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,&floating);
    if(compiled)glEndList();
    glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,&value);
    check(!glGetError()&&value==(compiled?0:2),"producer base setter has native compile semantics");
    glTexParameteri(GL_TEXTURE_2D,GL_GENERATE_MIPMAP,GL_TRUE);glFinish();
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj format=NULL;CGLContextObj consumer=NULL;GLint n=0;
    CGLError error=CGLChoosePixelFormat(attrs,&format,&n);
    if(!error&&format)error=CGLCreateContext(format,current,&consumer);
    if(format)CGLDestroyPixelFormat(format);
    check(!error&&consumer,"shared lower-update consumer created");
    if(!consumer)return;
    CGLSetCurrentContext(consumer);glBindTexture(GL_TEXTURE_2D,texture);
    if(compiled)glCallList(list);
    glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,&value);
    check(!glGetError()&&value==2,"consumer sees the selected shared base");
    glGetTexParameteriv(GL_TEXTURE_2D,GL_GENERATE_MIPMAP,&value);
    check(!glGetError()&&value==GL_TRUE,"consumer sees shared automatic generation");
    fill(pixels,256,modified);update(GL_TEXTURE_2D,0,16,pixels);
    check(!glGetError()&&matches(GL_TEXTURE_2D,0,16,GL_RGBA8,modified),"consumer writes the lower level");
    check(matches(GL_TEXTURE_2D,2,4,GL_RGBA8,original),"shared lower update preserves base");
    check(matches(GL_TEXTURE_2D,3,2,GL_RGBA8,updated),"shared lower update preserves child");
    GLuint fbo=0;unsigned char gpu[4]={0};glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,texture,3);
    glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
    check(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT,"shared child is a complete GPU attachment");
    glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,gpu);
    check(!glGetError()&&!memcmp(gpu,updated,4),"GPU child is unchanged after the shared lower update");
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);
    /* The possibility bit is sticky, but must not suppress healthy base-zero
     * generation after the application changes its selected base back. */
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,0);update(GL_TEXTURE_2D,0,16,pixels);
    check(!glGetError()&&matches(GL_TEXTURE_2D,1,8,GL_RGBA8,modified),"returning to base zero regenerates normally");
    check(matches(GL_TEXTURE_2D,4,1,GL_RGBA8,modified),"healthy base-zero generation reaches the last child");
    glGetTexParameteriv(GL_TEXTURE_2D,GL_GENERATE_MIPMAP,&value);check(value==GL_TRUE,"shared update retains automatic state");
    CGLSetCurrentContext(current);CGLDestroyContext(consumer);
    if(list)glDeleteLists(list,1);glDeleteTextures(1,&texture);check(!glGetError(),"shared lower controls leave no error");
}
static void list_case(GLenum mode)
{
    GLuint texture=0,list=glGenLists(1);unsigned char pixels[64];
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,0);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,2);
    glTexParameteri(GL_TEXTURE_2D,GL_GENERATE_MIPMAP,GL_TRUE);
    fill(pixels,16,original);define(GL_TEXTURE_2D,0,GL_RGBA8,4,pixels);
    printf("list mode=%x\n",mode);
    fill(pixels,16,updated);glNewList(list,mode);update(GL_TEXTURE_2D,0,4,pixels);glEndList();
    check(matches(GL_TEXTURE_2D,1,2,GL_RGBA8,mode==GL_COMPILE?original:updated),
        "automatic update respects list compilation versus execution");
    memset(pixels,0,sizeof(pixels));glCallList(list);
    check(matches(GL_TEXTURE_2D,1,2,GL_RGBA8,updated),"list playback regenerates from captured image");
    check(!glGetError(),"list automatic update leaves no error");
    glDeleteLists(list,1);glDeleteTextures(1,&texture);
}
static void pattern(void)
{
    unsigned char pixels[64],out[16];GLuint texture=0;
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexParameteri(GL_TEXTURE_2D,GL_GENERATE_MIPMAP,GL_TRUE);
    for(unsigned i=0;i<16;++i){pixels[i*4]=(i&1)?144:16;pixels[i*4+1]=(i&1)?176:80;
        pixels[i*4+2]=(i&1)?208:144;pixels[i*4+3]=64;}
    define(GL_TEXTURE_2D,0,GL_SRGB8_ALPHA8,4,pixels);
    memset(out,0,sizeof(out));glGetTexImage(GL_TEXTURE_2D,1,GL_RGBA,GL_UNSIGNED_BYTE,out);
    printf("OBSERVE sRGB patterned child=%u,%u,%u,%u error=%x (filter not asserted)\n",
        out[0],out[1],out[2],out[3],glGetError());glDeleteTextures(1,&texture);
}
static void update_controls(void)
{
    GLuint texture=0,pbo=0,fbo=0,rb=0;GLint value=0;unsigned char pixels[1024];
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexParameteri(GL_TEXTURE_2D,GL_GENERATE_MIPMAP,GL_FALSE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,2);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,2);
    for(int level=0;level<5;++level){int size=16>>level;fill(pixels,(unsigned)(size*size),original);
        define(GL_TEXTURE_2D,level,GL_RGBA8,size,pixels);}
    glTexParameteri(GL_TEXTURE_2D,GL_GENERATE_MIPMAP,GL_TRUE);
    check(!glGetError(),"update controls start with dense accepted source");
    fill(pixels,16,updated);glEnable(0xdeadbeef);update(GL_TEXTURE_2D,2,4,pixels);
    check(glGetError()==GL_INVALID_ENUM,"preexisting error survives a valid automatic update");
    check(matches(GL_TEXTURE_2D,3,2,GL_RGBA8,updated),"prior error does not suppress generation");
    fill(pixels,16,original);update_at(GL_TEXTURE_2D,2,4,4,pixels);
    check(glGetError()==GL_INVALID_VALUE,"invalid subimage retains native error");
    check(matches(GL_TEXTURE_2D,3,2,GL_RGBA8,updated),"invalid subimage does not regenerate children");
    glGenBuffers(1,&pbo);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,pbo);
    glBufferData(GL_PIXEL_UNPACK_BUFFER,64,pixels,GL_DYNAMIC_DRAW);update(GL_TEXTURE_2D,2,4,NULL);
    check(!glGetError()&&matches(GL_TEXTURE_2D,3,2,GL_RGBA8,original),"PBO upload generates children");
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&value);check(value==(GLint)pbo,"PBO unpack binding preserved");
    void *mapped=glMapBuffer(GL_PIXEL_UNPACK_BUFFER,GL_READ_ONLY);check(mapped&&!glGetError(),"PBO mapped control accepted");
    if(mapped){update(GL_TEXTURE_2D,2,4,NULL);check(glGetError()==GL_INVALID_OPERATION,"mapped PBO update rejected");glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);}
    check(matches(GL_TEXTURE_2D,3,2,GL_RGBA8,original),"mapped PBO rejection leaves child unchanged");
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glGenRenderbuffersEXT(1,&rb);glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,rb);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT,GL_RGBA8,4,4);
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_RENDERBUFFER_EXT,rb);
    glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
    check(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT&&!glGetError(),"copy source framebuffer complete");
    glClearColor(updated[0]/255.f,updated[1]/255.f,updated[2]/255.f,updated[3]/255.f);glClear(GL_COLOR_BUFFER_BIT);
    unsigned char source[4]={0};glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,source);
    check(!memcmp(source,updated,4)&&!glGetError(),"copy source has the expected GPU pixel");
    copy_update(1);check(!glGetError()&&matches(GL_TEXTURE_2D,3,2,GL_RGBA8,updated),"copy subimage generates child");
    check(matches(GL_TEXTURE_2D,2,4,GL_RGBA8,updated),"copy subimage preserves the updated base");
    glClearColor(original[0]/255.f,original[1]/255.f,original[2]/255.f,original[3]/255.f);glClear(GL_COLOR_BUFFER_BIT);
    copy_update(0);check(!glGetError()&&matches(GL_TEXTURE_2D,3,2,GL_RGBA8,original),"copy image generates child");
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&value);check(value==(GLint)fbo,"read framebuffer preserved");
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&value);check(value==(GLint)fbo,"draw framebuffer preserved");
    glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,&value);check(value==2,"clipped maximum preserved after all updates");
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);glDeleteRenderbuffersEXT(1,&rb);
    glDeleteBuffers(1,&pbo);glDeleteTextures(1,&texture);check(!glGetError(),"update controls leave no GL error");
}
static void list_upload_families(void)
{
    GLuint fbo=0,color=0;unsigned char pixels[16384];
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glGenRenderbuffersEXT(1,&color);glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,color);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT,GL_RGBA8,4,4);
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_RENDERBUFFER_EXT,color);
    glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
    glClearColor(updated[0]/255.f,updated[1]/255.f,updated[2]/255.f,updated[3]/255.f);glClear(GL_COLOR_BUFFER_BIT);
    check(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT&&!glGetError(),
        "list family copy source complete");
    const GLenum targets[]={GL_TEXTURE_1D,GL_TEXTURE_2D,GL_TEXTURE_3D};
    for(unsigned family=0;family<2;++family)for(unsigned dimension=0;dimension<3;++dimension)
        for(unsigned subimage=0;subimage<2;++subimage){
        if(family && dimension==2 && !subimage)continue; /* No CopyTexImage3D. */
        GLenum target=targets[dimension];GLuint texture=0,pbo=0;
        glGenTextures(1,&texture);glBindTexture(target,texture);
        glTexParameteri(target,GL_TEXTURE_BASE_LEVEL,2);glTexParameteri(target,GL_TEXTURE_MAX_LEVEL,4);
        for(int level=0;level<5;++level){int size=16>>level;
            fill(pixels,count(target,(unsigned)size),original);define(target,level,GL_RGBA8,size,pixels);}
        glTexParameteri(target,GL_GENERATE_MIPMAP,GL_TRUE);
        printf("list upload family=%s dimension=%u subimage=%u mode=%x\n",family?"copy":"PBO",dimension+1,subimage,upload_list_mode);
        if(family&&copy_auto_off)glTexParameteri(target,GL_GENERATE_MIPMAP,GL_FALSE);
        check(!glGetError(),"list upload source accepted");
        if(!family){
            glGenBuffers(1,&pbo);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,pbo);
            fill(pixels,count(target,4),updated);
            glBufferData(GL_PIXEL_UNPACK_BUFFER,(GLsizeiptr)count(target,4)*4,pixels,GL_STATIC_DRAW);
        }
        GLuint list=begin_upload();
        if(family){
            if(dimension==0){
                if(subimage)glCopyTexSubImage1D(target,2,0,0,0,4);
                else glCopyTexImage1D(target,2,GL_RGBA8,0,0,4,0);
            }else if(dimension==1){
                if(subimage)glCopyTexSubImage2D(target,2,0,0,0,0,4,4);
                else glCopyTexImage2D(target,2,GL_RGBA8,0,0,4,4,0);
            }else for(int z=0;z<4;++z)glCopyTexSubImage3D(target,2,0,0,z,0,0,4,4);
        }else if(dimension==0){
            if(subimage)glTexSubImage1D(target,2,0,4,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
            else glTexImage1D(target,2,GL_RGBA8,4,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        }else if(dimension==1){
            if(subimage)glTexSubImage2D(target,2,0,0,4,4,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
            else glTexImage2D(target,2,GL_RGBA8,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        }else{
            if(subimage)glTexSubImage3D(target,2,0,0,0,4,4,4,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
            else glTexImage3D(target,2,GL_RGBA8,4,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        }
        end_upload(list);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
        check(!glGetError(),"list family upload accepted");
        check(matches(target,2,4,GL_RGBA8,updated),"list family preserves uploaded base");
        check(matches(target,3,2,GL_RGBA8,updated),"list family generates the first child");
        check(matches(target,4,1,GL_RGBA8,updated),"list family generates the final child");
        if(pbo)glDeleteBuffers(1,&pbo);glDeleteTextures(1,&texture);
    }
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);glDeleteRenderbuffersEXT(1,&color);
    check(!glGetError(),"list family cleanup accepted");
}
static void parameters(CGLContextObj current)
{
    GLint value=0;GLuint fbo=0,color=0;
    glGenFramebuffersEXT(1,&fbo);glGenRenderbuffersEXT(1,&color);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,color);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT,GL_RGBA8,4,4);
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_RENDERBUFFER_EXT,color);
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
    check(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT&&!glGetError(),
        "parameter controls have a complete drawable for Begin End");
    glBindTexture(GL_TEXTURE_2D,0);glBindTexture(GL_TEXTURE_1D,0);
    glTexParameteri(GL_TEXTURE_2D,GL_GENERATE_MIPMAP,2);
    glGetTexParameteriv(GL_TEXTURE_2D,GL_GENERATE_MIPMAP,&value);
    GLenum observed=glGetError();printf("OBSERVE integer boolean 2 value=%d error=%x\n",value,observed);
    glTexParameteri(GL_TEXTURE_2D,GL_GENERATE_MIPMAP,GL_TRUE);
    glGetTexParameteriv(GL_TEXTURE_2D,GL_GENERATE_MIPMAP,&value);
    check(!glGetError()&&value==GL_TRUE,"integer enables default texture automatic generation");
    glTexParameterf(GL_TEXTURE_2D,GL_GENERATE_MIPMAP,.25f);
    glGetTexParameteriv(GL_TEXTURE_2D,GL_GENERATE_MIPMAP,&value);
    observed=glGetError();printf("OBSERVE float boolean .25 value=%d error=%x\n",value,observed);
    glTexParameterf(GL_TEXTURE_2D,GL_GENERATE_MIPMAP,1.f);
    glGetTexParameteriv(GL_TEXTURE_2D,GL_GENERATE_MIPMAP,&value);
    check(!glGetError()&&value==GL_TRUE,"float enables default texture automatic generation");
    glTexParameteri(GL_TEXTURE_1D,GL_GENERATE_MIPMAP,GL_FALSE);
    glGetTexParameteriv(GL_TEXTURE_2D,GL_GENERATE_MIPMAP,&value);
    check(!glGetError()&&value==GL_TRUE,"default texture targets have distinct parameter state");
    GLuint named=0;glGenTextures(1,&named);glBindTexture(GL_TEXTURE_2D,named);
    glTexParameteri(GL_TEXTURE_2D,GL_GENERATE_MIPMAP,GL_TRUE);
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj shared=NULL;GLint n=0;
    CGLError error=CGLChoosePixelFormat(attrs,&pf,&n);
    if(!error&&pf)error=CGLCreateContext(pf,current,&shared);
    if(pf)CGLDestroyPixelFormat(pf);
    check(!error&&shared,"shared context created");
    if(shared){
        CGLSetCurrentContext(shared);glBindTexture(GL_TEXTURE_2D,0);
        glGetTexParameteriv(GL_TEXTURE_2D,GL_GENERATE_MIPMAP,&value);
        check(!glGetError()&&value==GL_FALSE,"default texture state is context local");
        glBindTexture(GL_TEXTURE_2D,named);glGetTexParameteriv(GL_TEXTURE_2D,GL_GENERATE_MIPMAP,&value);
        check(!glGetError()&&value==GL_TRUE,"named texture state is shared");
        CGLSetCurrentContext(current);CGLDestroyContext(shared);
    }
    glDeleteTextures(1,&named);glBindTexture(GL_TEXTURE_2D,0);
    glGetTexParameteriv(GL_TEXTURE_2D,GL_GENERATE_MIPMAP,&value);
    check(!glGetError()&&value==GL_TRUE,"deleting named texture preserves default state");
    value=0x12345678;glGetTexParameteriv(GL_ARRAY_BUFFER,GL_GENERATE_MIPMAP,&value);
    check(glGetError()==GL_INVALID_ENUM&&value==0x12345678,"invalid query target preserves output and returns invalid enum");
    GLfloat floating=123.f;glGetTexParameterfv(GL_ARRAY_BUFFER,GL_GENERATE_MIPMAP,&floating);
    check(glGetError()==GL_INVALID_ENUM&&floating==123.f,"invalid float query preserves output");
    glBegin(GL_POINTS);glTexParameteri(GL_TEXTURE_2D,GL_GENERATE_MIPMAP,GL_FALSE);glEnd();
    check(glGetError()==GL_INVALID_OPERATION,"automatic parameter cannot change inside Begin End");
    glGetTexParameteriv(GL_TEXTURE_2D,GL_GENERATE_MIPMAP,&value);
    check(!glGetError()&&value==GL_TRUE,"rejected Begin End update preserves state");
    floating=123.f;glBegin(GL_POINTS);glGetTexParameterfv(GL_TEXTURE_2D,GL_GENERATE_MIPMAP,&floating);glEnd();
    check(glGetError()==GL_INVALID_OPERATION&&floating==123.f,"Begin End query preserves output");
    glTexParameteri(GL_TEXTURE_2D,GL_GENERATE_MIPMAP,GL_FALSE);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);glDeleteRenderbuffersEXT(1,&color);
}
int main(int argc,char **argv)
{
    int single=0,base=0,named=1,params=0,controls=0,families=0,lower=0,shared=-1,compiled=0;GLenum target=GL_TEXTURE_2D;GLint format=GL_RGBA8;
    for(int i=1;i<argc;++i){
        if(!strcmp(argv[i],"--dense"))dense=1;
        else if(!strcmp(argv[i],"--list"))upload_list_mode=GL_COMPILE;
        else if(!strcmp(argv[i],"--compile-execute"))upload_list_mode=GL_COMPILE_AND_EXECUTE;
        else if(!strcmp(argv[i],"--npot"))non_pot=1;
        else if(!strcmp(argv[i],"--clipped-max"))clipped_max=1;
#ifdef VIM_AUTOMATIC_NATIVE_CONTROL
        else if(!strcmp(argv[i],"--adapted"))adapted=1;
#endif
        else if(!strcmp(argv[i],"--parameters"))params=1;
        else if(!strcmp(argv[i],"--update-controls"))controls=1;
        else if(!strcmp(argv[i],"--list-upload-families"))families=1;
        else if(!strcmp(argv[i],"--copy-auto-off"))copy_auto_off=1;
        else if(!strcmp(argv[i],"--lower-controls"))lower=1;
        else if(!strcmp(argv[i],"--shared-lower-case")&&i+1<argc){shared=atoi(argv[++i]);if(shared<0||shared>3)return 2;}
        else if(!strcmp(argv[i],"--compiled-base"))compiled=1;
        else if(!strcmp(argv[i],"--case")&&i+4<argc){
            single=1;target=(GLenum)strtoul(argv[++i],NULL,0);format=(GLint)strtoul(argv[++i],NULL,0);
            base=atoi(argv[++i]);named=atoi(argv[++i]);
            if((target!=GL_TEXTURE_1D&&target!=GL_TEXTURE_2D&&target!=GL_TEXTURE_3D)||
               (format!=GL_RGB8&&format!=GL_RGBA8&&format!=GL_SRGB8_ALPHA8)||
               (base!=0&&base!=2)||(named!=0&&named!=1))return 2;
        }else return 2;
    }
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj context=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&context)||CGLSetCurrentContext(context))return 2;
    CGLDestroyPixelFormat(pf);
#ifdef VIM_AUTOMATIC_NATIVE_CONTROL
    DGMipmapCaptureExecution(context,&execution);
#endif
    printf("renderer=%s version=%s bits=%u\n",glGetString(GL_RENDERER),glGetString(GL_VERSION),(unsigned)sizeof(void*)*8);
    const char *version=(const char *)glGetString(GL_VERSION);int major=0,minor=0;
    if(version)sscanf(version,"%d.%d",&major,&minor);
    printf("SGIS_generate_mipmap advertised=%d core_1_4=%d\n",extension("GL_SGIS_generate_mipmap"),major>1||(major==1&&minor>=4));
    if(!extension("GL_SGIS_generate_mipmap")&&!(major>1||(major==1&&minor>=4))){
        puts("UNSUPPORTED automatic mipmap generation");return 77;
    }
    glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    const GLenum targets[]={GL_TEXTURE_1D,GL_TEXTURE_2D,GL_TEXTURE_3D};
    const GLint formats[]={GL_RGB8,GL_RGBA8,GL_SRGB8_ALPHA8};
    int srgb=major>2||(major==2&&minor>=1)||extension("GL_EXT_texture_sRGB");
    if(families){if(!upload_list_mode)return 2;list_upload_families();}
    else if(params)parameters(context);
    else if(shared>=0)shared_lower(context,shared,compiled);
    else if(lower)lower_controls();
    else if(controls)update_controls();
    else if(single){
        if(format==GL_SRGB8_ALPHA8&&!srgb){puts("UNSUPPORTED sRGB texture format");return 77;}
        run(target,format,base,named);
    }
    else {
        for(unsigned f=0;f<3;++f){
            if(f==2&&!srgb){++skipped;continue;}
            for(unsigned t=0;t<3;++t)for(int b=0;b<=2;b+=2)for(int object=0;object<2;++object)
                run(targets[t],formats[f],b,object);
        }
        list_case(GL_COMPILE);list_case(GL_COMPILE_AND_EXECUTE);
        if(srgb)pattern();
    }
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);
    printf("automatic-mipmap checks=%u failures=%u skipped_format_families=%u\n",checks,failures,skipped);
    return failures?1:0;
}
