/* HDR mip generation and state preservation through public CGL. Constant
 * values, including negative and >1 components, expose clamping/precision
 * loss without requiring a particular implementation's reduction filter. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <dlfcn.h>
/* Compatibility headers use EXT aliases; the cube-array core values live in
 * the SDK's separate gl3.h/gl3ext.h, which cannot be mixed with gl.h. */
#ifndef GL_TEXTURE_1D_ARRAY
#define GL_TEXTURE_1D_ARRAY GL_TEXTURE_1D_ARRAY_EXT
#define GL_TEXTURE_2D_ARRAY GL_TEXTURE_2D_ARRAY_EXT
#define GL_TEXTURE_BINDING_1D_ARRAY GL_TEXTURE_BINDING_1D_ARRAY_EXT
#define GL_TEXTURE_BINDING_2D_ARRAY GL_TEXTURE_BINDING_2D_ARRAY_EXT
#endif
#ifndef GL_TEXTURE_CUBE_MAP_ARRAY
#define GL_TEXTURE_CUBE_MAP_ARRAY 0x9009
#define GL_TEXTURE_BINDING_CUBE_MAP_ARRAY 0x900a
#endif
#ifndef VIM_MIPMAP_PUBLIC_ONLY
#include "../PassthroughOpenGL/passthrough-opengl-mipmap-host.h"
#include "../PassthroughOpenGL/passthrough-opengl-automatic-mipmap-host.h"
static GLenum automatic_error;
static GLenum control_error(void)
{GLenum error=glGetError();if(automatic_error){error=automatic_error;automatic_error=0;}return error;}
#define glGetError control_error
#endif
static unsigned checks,failures,unsupported;
static int native_only,legacy=1,lists,normalized,depth_mode,immutable,byte_mode,automatic,cube_only;
static void (*storage1)(GLenum,GLsizei,GLenum,GLsizei);
static void (*storage2)(GLenum,GLsizei,GLenum,GLsizei,GLsizei);
static void (*storage3)(GLenum,GLsizei,GLenum,GLsizei,GLsizei,GLsizei);
#ifndef VIM_MIPMAP_PUBLIC_ONLY
static struct DGMipmapExecution execution;
#endif
static void check(int ok,const char *what)
{++checks;failures+=!ok;printf("%s %s\n",ok?"PASS":"FAIL",what);fflush(stdout);}
static unsigned pixels(GLenum target,unsigned size)
{
    if(target==GL_TEXTURE_1D_ARRAY)return size*3;
    if(target==GL_TEXTURE_2D_ARRAY)return size*size*3;
    if(target==GL_TEXTURE_CUBE_MAP_ARRAY)return size*size*12;
    return size*(target==GL_TEXTURE_1D?1:size)*(target==GL_TEXTURE_3D?size:1);
}
static void fill(float *data,unsigned n,const float *seed)
{for(unsigned i=0;i<n*(depth_mode?1u:4u);++i)data[i]=seed[depth_mode?0:i%4];}
static void define(GLenum target,GLenum face,int level,GLint format,int size,const float *data)
{
#ifndef VIM_MIPMAP_PUBLIC_ONLY
    struct DGAutomaticMipmapScope scope=DGBeginAutomaticMipmapExecute(face,level,
        automatic&&!native_only,0,0,&automatic_error,0,1,&execution);
#endif
    GLenum input=depth_mode?GL_DEPTH_COMPONENT:GL_RGBA;
    if(immutable){
        if(target==GL_TEXTURE_1D)glTexSubImage1D(target,level,0,size,input,GL_FLOAT,data);
        else if(target==GL_TEXTURE_3D||target==GL_TEXTURE_2D_ARRAY||target==GL_TEXTURE_CUBE_MAP_ARRAY)
            glTexSubImage3D(target,level,0,0,0,size,size,target==GL_TEXTURE_3D?size:target==GL_TEXTURE_2D_ARRAY?3:12,input,GL_FLOAT,data);
        else glTexSubImage2D(face,level,0,0,size,target==GL_TEXTURE_1D_ARRAY?3:size,input,GL_FLOAT,data);
    }
    else if(target==GL_TEXTURE_1D)glTexImage1D(target,level,format,size,0,input,GL_FLOAT,data);
    else if(target==GL_TEXTURE_3D||target==GL_TEXTURE_2D_ARRAY||target==GL_TEXTURE_CUBE_MAP_ARRAY)
        glTexImage3D(target,level,format,size,size,target==GL_TEXTURE_3D?size:target==GL_TEXTURE_2D_ARRAY?3:12,0,input,GL_FLOAT,data);
    else if(target==GL_TEXTURE_1D_ARRAY)glTexImage2D(target,level,format,size,3,0,input,GL_FLOAT,data);
    else glTexImage2D(face,level,format,size,size,0,input,GL_FLOAT,data);
#ifndef VIM_MIPMAP_PUBLIC_ONLY
    DGEndAutomaticMipmap(&scope,&execution,&automatic_error);
#endif
}
static void gpu_child(GLenum target,GLenum face,int level,GLint format,const float *seed)
{
    if(!depth_mode&&format!=GL_RGB16F_ARB&&format!=GL_RGB32F_ARB&&format!=GL_RGBA16F_ARB&&format!=GL_RGBA32F_ARB&&
       format!=GL_R16F&&format!=GL_R32F&&format!=GL_RG16F&&format!=GL_RG32F&&
       format!=GL_R16&&format!=GL_RG16&&format!=GL_RGB16&&format!=GL_RGBA16&&
       format!=GL_R8&&format!=GL_RG8&&format!=GL_RGB8&&format!=GL_RGBA8)return;
    GLuint fbo=0;GLint texture=0,read=0,draw=0;GLfloat out[4]={0};
    GLenum binding=target==GL_TEXTURE_1D?GL_TEXTURE_BINDING_1D:target==GL_TEXTURE_3D?GL_TEXTURE_BINDING_3D:
        target==GL_TEXTURE_CUBE_MAP?GL_TEXTURE_BINDING_CUBE_MAP:target==GL_TEXTURE_1D_ARRAY?GL_TEXTURE_BINDING_1D_ARRAY:
        target==GL_TEXTURE_2D_ARRAY?GL_TEXTURE_BINDING_2D_ARRAY:
        target==GL_TEXTURE_CUBE_MAP_ARRAY?GL_TEXTURE_BINDING_CUBE_MAP_ARRAY:GL_TEXTURE_BINDING_2D;
    glGetIntegerv(binding,&texture);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    GLenum attachment=depth_mode?GL_DEPTH_ATTACHMENT_EXT:GL_COLOR_ATTACHMENT0_EXT;
    if(target==GL_TEXTURE_1D)glFramebufferTexture1DEXT(GL_FRAMEBUFFER_EXT,attachment,target,(GLuint)texture,level);
    else if(target==GL_TEXTURE_3D||target==GL_TEXTURE_1D_ARRAY||target==GL_TEXTURE_2D_ARRAY||target==GL_TEXTURE_CUBE_MAP_ARRAY)
        glFramebufferTextureLayerEXT(GL_FRAMEBUFFER_EXT,attachment,(GLuint)texture,level,0);
    else glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,attachment,face,(GLuint)texture,level);
    glReadBuffer(depth_mode?GL_NONE:GL_COLOR_ATTACHMENT0_EXT);glDrawBuffer(depth_mode?GL_NONE:GL_COLOR_ATTACHMENT0_EXT);
    GLenum status=glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
    if(status==GL_FRAMEBUFFER_COMPLETE_EXT)glReadPixels(0,0,1,1,depth_mode?GL_DEPTH_COMPONENT:GL_RGBA,GL_FLOAT,out);
    GLenum error=glGetError();int ok=status==GL_FRAMEBUFFER_COMPLETE_EXT&&!error;
    for(unsigned c=0;c<(depth_mode?1u:4u);++c){
        float expected=seed[c];
        if((format==GL_RGB16F_ARB||format==GL_RGB32F_ARB||format==GL_RGB16||format==GL_RGB8)&&c==3)expected=1;
        if(format==GL_R16F||format==GL_R32F||format==GL_R16||format==GL_R8){if(c==1||c==2)expected=0;if(c==3)expected=1;}
        if(format==GL_RG16F||format==GL_RG32F||format==GL_RG16||format==GL_RG8){if(c==2)expected=0;if(c==3)expected=1;}
        if(!isfinite(out[c])||fabsf(out[c]-expected)>(normalized?2.f/65535.f:.0001f))ok=0;
    }
    if(!ok)printf("HDR GPU format=%x level=%d status=%x error=%x values=%g,%g,%g,%g\n",format,level,status,error,out[0],out[1],out[2],out[3]);
    check(ok,"HDR child GPU attachment preserves floating components");
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,(GLuint)read);glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,(GLuint)draw);
    glDeleteFramebuffersEXT(1,&fbo);
}
static int match(GLenum target,GLenum face,int level,int size,GLint format,const float *seed)
{
    GLfloat out[4096];GLint width=0;
    glGetTexLevelParameteriv(face,level,GL_TEXTURE_WIDTH,&width);
    if(width!=size){printf("width level=%d actual=%d expected=%d\n",level,width,size);return 0;}
    glGetTexImage(face,level,depth_mode?GL_DEPTH_COMPONENT:GL_RGBA,GL_FLOAT,out);GLenum error=glGetError();
    if(error){printf("read error=%x\n",error);return 0;}
    for(unsigned i=0;i<pixels(target,(unsigned)size)*(depth_mode?1u:4u);++i){
        unsigned c=depth_mode?0:i%4;float expected=seed[c];
        switch(format){
        case GL_RGB16F_ARB:case GL_RGB32F_ARB:case GL_RGB16:case GL_RGB8:if(c==3)expected=1;break;
        case GL_R16F:case GL_R32F:case GL_R16:case GL_R8:if(c==1||c==2)expected=0;if(c==3)expected=1;break;
        case GL_RG16F:case GL_RG32F:case GL_RG16:case GL_RG8:if(c==2)expected=0;if(c==3)expected=1;break;
        case GL_ALPHA16F_ARB:case GL_ALPHA32F_ARB:case GL_ALPHA16:case GL_ALPHA8:if(c<3)expected=0;break;
        /* GetTexImage's table 6.1 expansion is NOT texture sampling's
         * luminance/intensity replication: absent green/blue return zero. */
        case GL_INTENSITY16F_ARB:case GL_INTENSITY32F_ARB:case GL_INTENSITY16:case GL_INTENSITY8:
        case GL_LUMINANCE16F_ARB:case GL_LUMINANCE32F_ARB:case GL_LUMINANCE16:case GL_LUMINANCE8:
            expected=c==0?seed[0]:c==3?1:0;break;
        case GL_LUMINANCE_ALPHA16F_ARB:case GL_LUMINANCE_ALPHA32F_ARB:case GL_LUMINANCE16_ALPHA16:case GL_LUMINANCE8_ALPHA8:
            if(c==1||c==2)expected=0;break;
        }
        if(!isfinite(out[i])||fabsf(out[i]-expected)>(normalized?2.f/65535.f:.0001f)){
            printf("pixel level=%d byte=%u actual=%g expected=%g\n",level,i,out[i],expected);return 0;
        }
    }return 1;
}
static void precision_controls(void)
{
    const GLenum formats[]={GL_RGBA8,GL_RGBA16F_ARB};
    const GLfloat seed[]={.12345f,.54321f,.87654f,.65432f};
    for(unsigned f=0;f<2;++f){
        GLuint texture=0;GLfloat out[4]={0};glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
        glTexImage2D(GL_TEXTURE_2D,0,(GLint)formats[f],1,1,0,GL_RGBA,GL_FLOAT,seed);
        glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,out);
        int plausible=!glGetError(),differs=0;
        for(unsigned c=0;c<4;++c){float delta=fabsf(out[c]-seed[c]);
            if(!isfinite(out[c])||delta>.005f)plausible=0;
            if(delta>2.f/65535.f)differs=1;
        }
        printf("narrow control format=%x values=%g,%g,%g,%g\n",formats[f],out[0],out[1],out[2],out[3]);
        check(plausible&&differs,"16-bit precision oracle rejects a byte or half-float intermediate");
        glDeleteTextures(1,&texture);
    }
}
static void run(GLenum target,GLint format,int base)
{
    GLuint texture=0,list=0;GLfloat data[16384],value[]={-2.25f,13.5f,.625f,1.5f};
    GLfloat other[]={7.25f,-.75f,3.125f,.5f};
    if(normalized||depth_mode||byte_mode){
        /* Not half-float/byte-grid values: either narrowed intermediate must
         * fail the normalized 16-bit precision check. Depth uses its own
         * constant to avoid making a reduction-filter algorithm assumption. */
        const GLfloat a[]={.12345f,.54321f,.87654f,.65432f},b[]={.23456f,.76543f,.45678f,.34567f};
        memcpy(value,a,sizeof(value));memcpy(other,b,sizeof(other));
        if(depth_mode){value[0]=.125f;other[0]=.25f;}
        if(byte_mode)for(unsigned c=0;c<4;++c){value[c]=roundf(value[c]*255.f)/255.f;other[c]=roundf(other[c]*255.f)/255.f;}
    }
    glGenTextures(1,&texture);glBindTexture(target,texture);
    printf("case target=%x format=%x base=%d immutable=%d\n",target,format,base,immutable);fflush(stdout);
    if(immutable){
        int size=4<<base;
        if(target==GL_TEXTURE_1D)storage1(target,base+3,(GLenum)format,size);
        else if(target==GL_TEXTURE_3D||target==GL_TEXTURE_2D_ARRAY||target==GL_TEXTURE_CUBE_MAP_ARRAY)
            storage3(target,base+3,(GLenum)format,size,size,target==GL_TEXTURE_3D?size:target==GL_TEXTURE_2D_ARRAY?3:12);
        else storage2(target,base+3,(GLenum)format,size,target==GL_TEXTURE_1D_ARRAY?3:size);
        GLenum error=glGetError();if(error){
            printf("UNSUPPORTED immutable format=%x target=%x error=%x\n",format,target,error);
            ++unsupported;glDeleteTextures(1,&texture);return;
        }
    }
    for(unsigned f=0;f<(target==GL_TEXTURE_CUBE_MAP?6u:1u);++f){
        GLenum face=target==GL_TEXTURE_CUBE_MAP?GL_TEXTURE_CUBE_MAP_POSITIVE_X+f:target;
        /* Define a dense valid prefix. The actual generation source is base. */
        for(int level=0;level<base;++level){int size=4<<(base-level);
            fill(data,pixels(target,(unsigned)size),other);define(target,face,level,format,size,data);}
        fill(data,pixels(target,4),value);define(target,face,base,format,4,data);
        fill(data,pixels(target,1),other);define(target,face,base+(immutable?2:3),format,1,data);
    }
    GLenum source_error=glGetError();
    if(source_error){printf("UNSUPPORTED format=%x target=%x source_error=%x\n",format,target,source_error);
        ++unsupported;glDeleteTextures(1,&texture);return;}
    glTexParameteri(target,GL_TEXTURE_BASE_LEVEL,base);glTexParameteri(target,GL_TEXTURE_MAX_LEVEL,base+(immutable?1:2));
    check(!glGetError(),"HDR source range accepted");
    GLenum face=target==GL_TEXTURE_CUBE_MAP?GL_TEXTURE_CUBE_MAP_POSITIVE_X:target;
    check(match(target,face,base,4,format,value),"HDR source retains unclamped floating components");
    GLenum error=GL_NO_ERROR;
    if(lists){list=glGenLists(1);glColor4f(.125f,.25f,.375f,.5f);
        glNewList(list,GL_COMPILE);glColor4f(.75f,.5f,.25f,1);}
    if(automatic){
        glTexParameteri(target,GL_GENERATE_MIPMAP,GL_TRUE);
        fill(data,pixels(target,4),value);
        for(unsigned f=0;f<(target==GL_TEXTURE_CUBE_MAP?6u:1u);++f)
            define(target,target==GL_TEXTURE_CUBE_MAP?GL_TEXTURE_CUBE_MAP_POSITIVE_X+f:target,base,format,4,data);
        glTexParameteri(target,GL_GENERATE_MIPMAP,GL_FALSE);
    }else{
#ifdef VIM_MIPMAP_PUBLIC_ONLY
    glGenerateMipmapEXT(target);
#else
    if(native_only)glGenerateMipmapEXT(target);else error=DGGenerateSrgbMipmapExecute(target,legacy,0,&execution);
#endif
    }
    if(!error)error=glGetError();check(!error,"HDR generation accepted");
    if(lists){
        GLint current=0;GLfloat color[4]={0};glGetIntegerv(GL_LIST_INDEX,&current);glGetFloatv(GL_CURRENT_COLOR,color);
        check(current==(GLint)list&&color[0]==.125f,"HDR generator executes without consuming caller list");
        glEndList();
    }
    for(unsigned f=0;f<(target==GL_TEXTURE_CUBE_MAP?6u:1u);++f){
        face=target==GL_TEXTURE_CUBE_MAP?GL_TEXTURE_CUBE_MAP_POSITIVE_X+f:target;
        check(match(target,face,base,4,format,value),"HDR generation preserves base");
        check(match(target,face,base+1,2,format,value),"HDR first child preserves floating components");
        gpu_child(target,face,base+1,format,value);
        if(!immutable)check(match(target,face,base+2,1,format,value),"HDR last child preserves floating components");
        check(match(target,face,base+(immutable?2:3),1,format,other),"HDR generation preserves excluded tail");
        if(base)check(match(target,face,base-1,8,format,other),"HDR generation preserves lower level");
    }
    GLint state=0;glGetTexParameteriv(target,GL_TEXTURE_BASE_LEVEL,&state);check(state==base,"HDR base parameter preserved");
    glGetTexParameteriv(target,GL_TEXTURE_MAX_LEVEL,&state);check(state==base+(immutable?1:2),"HDR max parameter preserved");
    if(immutable){glGetTexParameteriv(target,0x912f,&state);check(state==GL_TRUE,"immutable storage is not redefined by generation");}
    if(lists){
        fill(data,pixels(target,2),other);
        for(unsigned f=0;f<(target==GL_TEXTURE_CUBE_MAP?6u:1u);++f)
            define(target,target==GL_TEXTURE_CUBE_MAP?GL_TEXTURE_CUBE_MAP_POSITIVE_X+f:target,base+1,format,2,data);
        glCallList(list);GLfloat color[4]={0};glGetFloatv(GL_CURRENT_COLOR,color);
        check(color[0]==.75f,"caller color command survives HDR helper");
        check(match(target,face,base+1,2,format,other),"list playback does not replay HDR internal transfers");
        glDeleteLists(list,1);
    }
    check(!glGetError(),"HDR case leaves no error");glDeleteTextures(1,&texture);
}
int main(int argc,char **argv)
{
    for(int i=1;i<argc;++i){if(!strcmp(argv[i],"--native")||!strcmp(argv[i],"--executor"))native_only=1;
        else if(!strcmp(argv[i],"--core"))legacy=0;
        else if(!strcmp(argv[i],"--lists"))lists=1;
        else if(!strcmp(argv[i],"--normalized"))normalized=1;
        else if(!strcmp(argv[i],"--depth"))depth_mode=1;
        else if(!strcmp(argv[i],"--immutable"))immutable=1;
        else if(!strcmp(argv[i],"--automatic"))automatic=1;
        else if(!strcmp(argv[i],"--cube-only"))cube_only=1;
        else if(!strcmp(argv[i],"--byte"))byte_mode=1;else return 2;}
    if((lists&&!legacy)||(normalized+depth_mode+byte_mode>1)||(automatic&&(!legacy||immutable||lists)))return 2;
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        legacy?0:kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj context=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&context)||CGLSetCurrentContext(context))return 2;CGLDestroyPixelFormat(pf);
    printf("renderer=%s version=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    if(immutable){
        storage1=(void *)dlsym(RTLD_DEFAULT,"glTexStorage1D");storage2=(void *)dlsym(RTLD_DEFAULT,"glTexStorage2D");
        storage3=(void *)dlsym(RTLD_DEFAULT,"glTexStorage3D");
        if(!storage1||!storage2||!storage3){puts("UNSUPPORTED public immutable storage entry points");return 77;}
    }
#ifndef VIM_MIPMAP_PUBLIC_ONLY
    if(!native_only)DGMipmapCaptureExecution(context,&execution);
#endif
    glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glClampColorARB(GL_CLAMP_READ_COLOR_ARB,GL_FALSE);
    check(!glGetError(),"floating GPU readback does not clamp to normalized color");
    if(normalized)precision_controls();
    const GLenum targets[]={GL_TEXTURE_1D,GL_TEXTURE_2D,GL_TEXTURE_3D,GL_TEXTURE_CUBE_MAP,
        GL_TEXTURE_1D_ARRAY,GL_TEXTURE_2D_ARRAY,GL_TEXTURE_CUBE_MAP_ARRAY};
    const GLint floats[]={GL_RGB16F_ARB,GL_RGBA16F_ARB,GL_RGB32F_ARB,GL_RGBA32F_ARB,
        GL_R16F,GL_R32F,GL_RG16F,GL_RG32F,GL_ALPHA16F_ARB,GL_ALPHA32F_ARB,
        GL_INTENSITY16F_ARB,GL_INTENSITY32F_ARB,GL_LUMINANCE16F_ARB,GL_LUMINANCE32F_ARB,
        GL_LUMINANCE_ALPHA16F_ARB,GL_LUMINANCE_ALPHA32F_ARB};
    const GLint unorms[]={GL_R16,GL_RG16,GL_RGB16,GL_RGBA16,
        GL_ALPHA16,GL_INTENSITY16,GL_LUMINANCE16,GL_LUMINANCE16_ALPHA16};
    const GLint depths[]={GL_DEPTH_COMPONENT16,GL_DEPTH_COMPONENT24,GL_DEPTH_COMPONENT32,GL_DEPTH_COMPONENT32F};
    const GLint bytes[]={GL_R8,GL_RG8,GL_RGB8,GL_RGBA8,GL_ALPHA8,GL_INTENSITY8,GL_LUMINANCE8,GL_LUMINANCE8_ALPHA8};
    const GLint *formats=depth_mode?depths:byte_mode?bytes:normalized?unorms:floats;
    unsigned format_count=depth_mode?sizeof(depths)/sizeof(depths[0]):byte_mode?sizeof(bytes)/sizeof(bytes[0]):normalized?sizeof(unorms)/sizeof(unorms[0]):sizeof(floats)/sizeof(floats[0]);
    for(unsigned f=0;f<format_count;++f)
        for(unsigned t=0;t<(legacy?4u:7u);++t)if(!cube_only||targets[t]==GL_TEXTURE_CUBE_MAP)
            for(int base=0;base<=2;base+=2)run(targets[t],formats[f],base);
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);
    printf("float-mipmap checks=%u failures=%u unsupported=%u\n",checks,failures,unsupported);return failures?1:0;
}
