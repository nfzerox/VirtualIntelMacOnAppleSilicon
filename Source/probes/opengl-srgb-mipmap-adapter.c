/* Public GL pixels and state controls for the executor's exact mip adapter. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <dlfcn.h>
#include "../PassthroughOpenGL/passthrough-opengl-mipmap-host.h"
static int failures,native_only,luminance,decode,immutable,linear,dense;
static struct DGMipmapExecution execution;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line=%d %s\n",__LINE__,#x);++failures;}}while(0)
static void allocate(GLenum target,GLenum face,GLint base,GLenum format,GLsizei width,
    GLsizei height,GLsizei depth,const void *data)
{
    if(immutable){
        if(target==GL_TEXTURE_1D)glTexSubImage1D(target,base,0,width,GL_RGBA,GL_UNSIGNED_BYTE,data);
        else if(target==GL_TEXTURE_2D||target==GL_TEXTURE_CUBE_MAP||target==0x8c18)
            glTexSubImage2D(face,base,0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,data);
        else glTexSubImage3D(target,base,0,0,0,width,height,depth,GL_RGBA,GL_UNSIGNED_BYTE,data);
    }
    else if(target==GL_TEXTURE_1D)glTexImage1D(target,base,format,width,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
    else if(target==GL_TEXTURE_2D||target==GL_TEXTURE_CUBE_MAP||target==0x8c18)
        glTexImage2D(face,base,format,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
    else glTexImage3D(target,base,format,width,height,depth,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
}
static int filtered(int center,int amplitude)
{
    if(!decode||linear)return center;
    double a=(center-amplitude)/255.0,b=(center+amplitude)/255.0;
    a=a<=.04045?a/12.92:pow((a+.055)/1.055,2.4);
    b=b<=.04045?b/12.92:pow((b+.055)/1.055,2.4);
    double mean=(a+b)/2;mean=mean<.0031308?12.92*mean:1.055*pow(mean,1/2.4)-.055;
    return (int)lround(mean*255);
}
static void verify(GLenum target,int legacy,int with_alpha,GLint base,int size,int named,int last)
{
    unsigned start_failures=(unsigned)failures;
    GLuint tex=0,fbo[2]={0},pbo[2]={0};if(named)glGenTextures(1,&tex);glBindTexture(target,tex);
    int cube=target==GL_TEXTURE_CUBE_MAP,array1=target==0x8c18;
    int array2=target==0x8c1a||target==0x9009,volume=target==GL_TEXTURE_3D;
    int width=size,height=target==GL_TEXTURE_1D?1:size,depth=volume?size:array2?(target==0x9009?12:3):1;
    if(array1)height=3;
    int layers=cube?6:array1?height:array2?depth:1;
    size_t bytes=(size_t)width*height*depth*4;
    unsigned char *input=malloc(bytes),*output=malloc(bytes);CHECK(input&&output);
    if(!input||!output){free(input);free(output);return;}
    GLenum internal=luminance?(with_alpha?GL_SLUMINANCE8_ALPHA8:GL_SLUMINANCE8):linear?
        (with_alpha?GL_RGBA8:GL_RGB8):(with_alpha?GL_SRGB8_ALPHA8:GL_SRGB8);
    if(immutable){
        typedef void (*Storage1)(GLenum,GLsizei,GLenum,GLsizei);
        typedef void (*Storage2)(GLenum,GLsizei,GLenum,GLsizei,GLsizei);
        typedef void (*Storage3)(GLenum,GLsizei,GLenum,GLsizei,GLsizei,GLsizei);
        Storage1 one=(Storage1)dlsym(RTLD_DEFAULT,"glTexStorage1D");
        Storage2 two=(Storage2)dlsym(RTLD_DEFAULT,"glTexStorage2D");
        Storage3 three=(Storage3)dlsym(RTLD_DEFAULT,"glTexStorage3D");
        CHECK(one&&two&&three);if(!one||!two||!three)goto done;
        int initial_width=width<<base,initial_height=array1?height:height<<base;
        if(target==GL_TEXTURE_1D)one(target,base+last+1,internal,initial_width);
        else if(target==GL_TEXTURE_2D||cube||array1)two(target,base+last+1,internal,initial_width,initial_height);
        else three(target,base+last+1,internal,initial_width,initial_height,volume?depth<<base:depth);
        CHECK(glGetError()==GL_NO_ERROR);
    }
    if(immutable||dense){
        for(int level=0;level<base;++level){
            int pw=width<<(base-level),ph=target==GL_TEXTURE_1D?1:array1?height:height<<(base-level),pd=volume?depth<<(base-level):depth;
            size_t count=(size_t)pw*ph*pd*4;unsigned char *prefix=malloc(count);CHECK(prefix!=NULL);if(!prefix)goto done;
            for(int face=0;face<(cube?6:1);++face){
                for(size_t i=0;i<count;i+=4){prefix[i]=(unsigned char)(21+level);prefix[i+1]=(unsigned char)(31+face);prefix[i+2]=41;prefix[i+3]=170;}
                allocate(target,cube?GL_TEXTURE_CUBE_MAP_POSITIVE_X+(GLenum)face:target,level,internal,pw,ph,pd,prefix);
            }
            free(prefix);
        }
    }
    if(dense&&!immutable){
        int tw=width>>(last+1),th=array1?height:height>>(last+1),td=volume?depth>>(last+1):depth;
        if(!tw)tw=1;if(!th)th=1;if(!td)td=1;
        for(int face=0;face<(cube?6:1);++face){
            for(int i=0;i<tw*th*td*4;i+=4){input[i]=201;input[i+1]=(unsigned char)(101+face);input[i+2]=51;input[i+3]=171;}
            allocate(target,cube?GL_TEXTURE_CUBE_MAP_POSITIVE_X+(GLenum)face:target,base+last+1,internal,tw,th,td,input);
        }
    }
    for(int face=0;face<(cube?6:1);++face){
        for(int z=0;z<depth;++z)for(int y=0;y<height;++y)for(int x=0;x<width;++x){
            int layer=cube?face:array1?y:array2?z:0;
            unsigned char *pixel=input+(((size_t)z*height+y)*width+x)*4;
            pixel[0]=(unsigned char)(80+layer+(x&1?64:-64));
            pixel[1]=(unsigned char)(128+layer+(x&1?48:-48));
            pixel[2]=(unsigned char)(176+layer+(x&1?32:-32));pixel[3]=(unsigned char)(64+layer);
        }
        GLenum image=cube?GL_TEXTURE_CUBE_MAP_POSITIVE_X+(GLenum)face:target;
        allocate(target,image,base,internal,width,height,depth,input);
    }
    glTexParameteri(target,GL_TEXTURE_BASE_LEVEL,base);glTexParameteri(target,GL_TEXTURE_MAX_LEVEL,base+last);
    glTexParameteri(target,GL_TEXTURE_SRGB_DECODE_EXT,decode?GL_DECODE_EXT:GL_SKIP_DECODE_EXT);
    memset(output,0,bytes);
    glGetTexImage(cube?GL_TEXTURE_CUBE_MAP_POSITIVE_X:target,base,GL_RGBA,GL_UNSIGNED_BYTE,output);
    CHECK(output[0]==16&&output[1]==(luminance?0:80)&&output[2]==(luminance?0:144));
    glGenFramebuffersEXT(2,fbo);glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,fbo[0]);
    glReadBuffer(GL_NONE);glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,fbo[1]);glDrawBuffer(GL_NONE);
    glGenBuffers(2,pbo);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,pbo[0]);glBufferData(GL_PIXEL_UNPACK_BUFFER,32,NULL,GL_STATIC_DRAW);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,pbo[1]);glBufferData(GL_PIXEL_PACK_BUFFER,32,NULL,GL_STATIC_DRAW);
    glPixelStorei(GL_PACK_ALIGNMENT,8);glPixelStorei(GL_PACK_ROW_LENGTH,19);glPixelStorei(GL_PACK_SKIP_PIXELS,2);
    glPixelStorei(GL_PACK_ROW_BYTES_APPLE,128);glPixelStorei(GL_UNPACK_ROW_BYTES_APPLE,128);
    glPixelStorei(GL_UNPACK_ALIGNMENT,8);glPixelStorei(GL_UNPACK_ROW_LENGTH,17);glPixelStorei(GL_UNPACK_SKIP_PIXELS,3);
    glEnable(GL_FRAMEBUFFER_SRGB_EXT);
    if(legacy){glPixelTransferf(GL_RED_SCALE,.25f);glPixelTransferf(GL_ALPHA_BIAS,.25f);glPixelTransferi(GL_MAP_COLOR,GL_TRUE);}
    CHECK(glGetError()==GL_NO_ERROR);
    GLenum error;
    if(native_only){glGenerateMipmapEXT(target);error=glGetError();}
    else error=DGGenerateSrgbMipmapExecute(target,legacy,0,&execution);
    if(error)fprintf(stderr,"generation error=%x target=%x base=%d native=%d\n",error,target,base,native_only);
    CHECK(error==GL_NO_ERROR);
    GLint value=0;glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&value);CHECK(value==(GLint)fbo[0]);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&value);CHECK(value==(GLint)fbo[1]);
    glGetIntegerv(GL_READ_BUFFER,&value);CHECK(value==GL_NONE);glGetIntegerv(GL_DRAW_BUFFER,&value);CHECK(value==GL_NONE);
    glGetIntegerv(DGMipmapBinding(target),&value);CHECK(value==(GLint)tex);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&value);CHECK(value==(GLint)pbo[0]);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&value);CHECK(value==(GLint)pbo[1]);
    glGetIntegerv(GL_PACK_ALIGNMENT,&value);CHECK(value==8);
    glGetIntegerv(GL_PACK_ROW_LENGTH,&value);CHECK(value==19);
    glGetIntegerv(GL_PACK_SKIP_PIXELS,&value);CHECK(value==2);
    glGetIntegerv(GL_PACK_ROW_BYTES_APPLE,&value);CHECK(value==128);
    glGetIntegerv(GL_UNPACK_ROW_BYTES_APPLE,&value);CHECK(value==128);
    glGetIntegerv(GL_UNPACK_ALIGNMENT,&value);CHECK(value==8);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH,&value);CHECK(value==17);
    glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&value);CHECK(value==3);CHECK(glIsEnabled(GL_FRAMEBUFFER_SRGB_EXT));
    glGetTexParameteriv(target,GL_TEXTURE_BASE_LEVEL,&value);CHECK(value==base);
    glGetTexParameteriv(target,GL_TEXTURE_MAX_LEVEL,&value);CHECK(value==base+last);
    if(immutable){glGetTexParameteriv(target,0x912f,&value);CHECK(value==GL_TRUE);}
    if(legacy){GLfloat v=0;glGetFloatv(GL_RED_SCALE,&v);CHECK(v==.25f);glGetFloatv(GL_ALPHA_BIAS,&v);CHECK(v==.25f);
        glGetIntegerv(GL_MAP_COLOR,&value);CHECK(value==GL_TRUE);
        glPixelTransferf(GL_RED_SCALE,1);glPixelTransferf(GL_ALPHA_BIAS,0);glPixelTransferi(GL_MAP_COLOR,GL_FALSE);}
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glPixelStorei(GL_PACK_ALIGNMENT,4);
    glPixelStorei(GL_PACK_ROW_LENGTH,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);glPixelStorei(GL_PACK_ROW_BYTES_APPLE,0);
    for(int face=0;face<(cube?6:1);++face){
        glGetTexImage(cube?GL_TEXTURE_CUBE_MAP_POSITIVE_X+(GLenum)face:target,base,GL_RGBA,GL_UNSIGNED_BYTE,output);
        int unchanged=!glGetError();
        for(int z=0;z<depth&&unchanged;++z)for(int y=0;y<height&&unchanged;++y)for(int x=0;x<width;++x){
            int layer=cube?face:array1?y:array2?z:0;
            unsigned char *p=output+(((size_t)z*height+y)*width+x)*4;
            if(p[0]!=(unsigned char)(80+layer+(x&1?64:-64))||
               p[1]!=(luminance?0:(unsigned char)(128+layer+(x&1?48:-48)))||
               p[2]!=(luminance?0:(unsigned char)(176+layer+(x&1?32:-32)))||
               p[3]!=(with_alpha?64+layer:255)){unchanged=0;break;}
        }
        CHECK(unchanged);
    }
    if(immutable||dense){
        glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,fbo[0]);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
        for(int level=0;level<base;++level){
            int pw=width<<(base-level),ph=target==GL_TEXTURE_1D?1:array1?height:height<<(base-level),pd=volume?depth<<(base-level):depth;
            size_t count=(size_t)pw*ph*pd*4;unsigned char *prefix=malloc(count);CHECK(prefix!=NULL);if(!prefix)continue;
            for(int face=0;face<(cube?6:1);++face){
                glGetTexImage(cube?GL_TEXTURE_CUBE_MAP_POSITIVE_X+(GLenum)face:target,level,GL_RGBA,GL_UNSIGNED_BYTE,prefix);
                int preserved=!glGetError();
                for(size_t i=0;i<count;i+=4)if(prefix[i]!=21+level||prefix[i+1]!=(luminance?0:31+face)||prefix[i+2]!=(luminance?0:41)||prefix[i+3]!=(with_alpha?170:255)){preserved=0;break;}
                CHECK(preserved);
                if(!luminance&&named){
                    DGMipmapAttach(target,tex,level,face);CHECK(glCheckFramebufferStatusEXT(GL_READ_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
                    glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,output);
                    CHECK(glGetError()==GL_NO_ERROR&&output[0]==21+level&&output[1]==31+face&&output[2]==41&&output[3]==(with_alpha?170:255));
                }
            }
            free(prefix);
        }
        for(int layer=0;!luminance&&named&&layer<(volume?depth:layers);++layer){
            DGMipmapAttach(target,tex,base,layer);
            CHECK(glCheckFramebufferStatusEXT(GL_READ_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
            glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,output);
            int expected_layer=volume?0:layer;
            CHECK(glGetError()==GL_NO_ERROR && output[0]==16+expected_layer && output[1]==80+expected_layer &&
                output[2]==144+expected_layer && output[3]==(with_alpha?64+expected_layer:255));
        }
    }
    if(dense&&!immutable){
        int tw=width>>(last+1),th=array1?height:height>>(last+1),td=volume?depth>>(last+1):depth;
        if(!tw)tw=1;if(!th)th=1;if(!td)td=1;
        for(int face=0;face<(cube?6:1);++face){
            glGetTexImage(cube?GL_TEXTURE_CUBE_MAP_POSITIVE_X+(GLenum)face:target,base+last+1,GL_RGBA,GL_UNSIGNED_BYTE,output);
            int preserved=!glGetError();for(int i=0;i<tw*th*td*4;i+=4)
                if(output[i]!=201||output[i+1]!=(luminance?0:101+face)||output[i+2]!=(luminance?0:51)||output[i+3]!=(with_alpha?171:255)){preserved=0;break;}
            CHECK(preserved);
        }
    }
    for(int face=0;face<(cube?6:1);++face)for(int level=1;level<=last;++level){
        GLenum image=cube?GL_TEXTURE_CUBE_MAP_POSITIVE_X+(GLenum)face:target;
        int w=width>>level,h=array1?height:height>>level,d=volume?depth>>level:depth;
        if(!w)w=1;if(!h)h=1;if(!d)d=1;
        memset(output,0,bytes);glGetTexImage(image,base+level,GL_RGBA,GL_UNSIGNED_BYTE,output);
        for(int z=0;z<d;++z)for(int y=0;y<h;++y)for(int x=0;x<w;++x){
            int layer=cube?face:array1?y:array2?z:0;
            unsigned char *pixel=output+(((size_t)z*h+y)*w+x)*4;
            if(abs(pixel[0]-filtered(80+layer,64))>1||
               abs(pixel[1]-(luminance?0:filtered(128+layer,48)))>1||
               abs(pixel[2]-(luminance?0:filtered(176+layer,32)))>1||
               pixel[3]!=(with_alpha?64+layer:255)){
                fprintf(stderr,"FAIL pixels target=%x base=%d level=%d layer=%d value=%u,%u,%u,%u\n",
                    target,base,level,layer,pixel[0],pixel[1],pixel[2],pixel[3]);++failures;goto pixels_done;
            }
        }
    }
pixels_done:
    CHECK(glGetError()==GL_NO_ERROR);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,0);glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glDeleteBuffers(2,pbo);
    glPixelStorei(GL_UNPACK_ALIGNMENT,4);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
    glPixelStorei(GL_UNPACK_ROW_BYTES_APPLE,0);
    glDeleteFramebuffersEXT(2,fbo);if(named)glDeleteTextures(1,&tex);free(input);free(output);
    printf("%s target=%x alpha=%d base=%d layers=%d size=%d named=%d levels=%d\n",failures==(int)start_failures?"PASS":"FAIL",target,with_alpha,base,layers,size,named,last);
    return;
done:
    if(named)glDeleteTextures(1,&tex);free(input);free(output);
}
static void errors(int legacy)
{
    GLuint texture=0,fbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    const unsigned char value[4]={32,64,128,255};
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,value);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,texture,0);
    CHECK(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    glEnable(0xffffffffu);
    GLenum error;
    if(native_only){glGenerateMipmapEXT(GL_TEXTURE_2D);error=glGetError();}
    else {error=DGGenerateSrgbMipmapExecute(GL_TEXTURE_2D,legacy,0,&execution);if(!error)error=glGetError();}
    CHECK(error==GL_INVALID_ENUM);CHECK(glGetError()==GL_NO_ERROR);
    if(native_only)glGenerateMipmapEXT(GL_ARRAY_BUFFER);
    else CHECK(DGGenerateSrgbMipmapExecute(GL_ARRAY_BUFFER,legacy,0,&execution)==GL_NO_ERROR);
    CHECK(glGetError()==GL_INVALID_ENUM);CHECK(glGetError()==GL_NO_ERROR);
    if(legacy){glBegin(GL_POINTS);
        if(native_only)glGenerateMipmapEXT(GL_TEXTURE_2D);
        else DGGenerateSrgbMipmapExecute(GL_TEXTURE_2D,legacy,1,&execution);
        glEnd();CHECK(glGetError()==GL_INVALID_OPERATION);CHECK(glGetError()==GL_NO_ERROR);}
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&texture);
}
static void immutable_edges(void)
{
    typedef void (*Storage)(GLenum,GLsizei,GLenum,GLsizei,GLsizei);
    Storage storage=(Storage)dlsym(RTLD_DEFAULT,"glTexStorage2D");CHECK(storage!=NULL);if(!storage)return;
    const GLint ranges[][2]={{2,0},{999,1000},{2,999},{2,2},{3,999}};
    for(unsigned r=0;r<5;++r)for(int skip=0;skip<2;++skip){
        GLuint texture=0,fbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
        storage(GL_TEXTURE_2D,4,linear?GL_RGBA8:GL_SRGB8_ALPHA8,8,8);unsigned char pixels[256],out[256];
        for(unsigned level=0;level<4;++level){
            unsigned size=8>>level;for(unsigned i=0;i<size*size*4;i+=4){pixels[i]=20+level;pixels[i+1]=30+level;pixels[i+2]=40+level;pixels[i+3]=100+level;}
            glTexSubImage2D(GL_TEXTURE_2D,level,0,0,size,size,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        }
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,ranges[r][0]);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,ranges[r][1]);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_SRGB_DECODE_EXT,skip?GL_SKIP_DECODE_EXT:GL_DECODE_EXT);
        GLint saved_base=0,saved_max=0;glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,&saved_base);
        glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,&saved_max);
        CHECK(glGetError()==GL_NO_ERROR);
        if(r==0)glEnable(0xffffffffu);
        GLenum error;
        if(native_only){glGenerateMipmapEXT(GL_TEXTURE_2D);error=glGetError();}
        else error=DGGenerateSrgbMipmapExecute(GL_TEXTURE_2D,0,0,&execution);
        CHECK(error==(r==0?GL_INVALID_ENUM:GL_NO_ERROR));CHECK(glGetError()==GL_NO_ERROR);
        glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,fbo);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
        for(unsigned level=0;level<4;++level){
            unsigned size=8>>level,source=r==2&&level==3?2:level;
            glGetTexImage(GL_TEXTURE_2D,level,GL_RGBA,GL_UNSIGNED_BYTE,out);
            int good=!glGetError();for(unsigned i=0;i<size*size*4;i+=4)
                if(out[i]!=20+source||out[i+1]!=30+source||out[i+2]!=40+source||out[i+3]!=100+source){good=0;break;}
            CHECK(good);
            DGMipmapAttach(GL_TEXTURE_2D,texture,level,0);CHECK(glCheckFramebufferStatusEXT(GL_READ_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
            glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,out);
            CHECK(glGetError()==GL_NO_ERROR&&out[0]==20+source&&out[1]==30+source&&out[2]==40+source&&out[3]==100+source);
        }
        GLint value=0;glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,&value);CHECK(value==saved_base);
        glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,&value);CHECK(value==saved_max);
        glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&texture);
    }
}
int main(int argc,char **argv)
{
    int legacy=0;
    for(int i=1;i<argc;++i){if(!strcmp(argv[i],"--legacy"))legacy=1;
        else if(!strcmp(argv[i],"--native")||!strcmp(argv[i],"--executor"))native_only=1;
        else if(!strcmp(argv[i],"--sluminance"))luminance=legacy=1;
        else if(!strcmp(argv[i],"--decode"))decode=1;
        else if(!strcmp(argv[i],"--linear"))linear=1;
        else if(!strcmp(argv[i],"--dense"))dense=1;
        else if(!strcmp(argv[i],"--immutable"))immutable=1;else return 2;}
    if(linear&&luminance)return 2;
    const CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)(legacy?kCGLOGLPVersion_Legacy:kCGLOGLPVersion_GL4_Core),0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attrs,&format,&count)||!format)return 77;
    if(CGLCreateContext(format,NULL,&context)||CGLSetCurrentContext(context))return 2;CGLDestroyPixelFormat(format);
    if(!native_only)DGMipmapCaptureExecution(context,&execution);
    printf("mipmap adapter renderer=%s legacy=%d\n",glGetString(GL_RENDERER),legacy);
    const GLenum targets[]={GL_TEXTURE_1D,GL_TEXTURE_2D,GL_TEXTURE_3D,GL_TEXTURE_CUBE_MAP,0x8c18,0x8c1a,0x9009};
    unsigned targets_count=legacy?4:7;
    for(unsigned t=0;t<targets_count;++t)for(int alpha=0;alpha<2;++alpha)for(int base=0;base<=2;base+=2)
        for(int size=4;size<=6;size+=2)for(int last=1;last<=2;++last)
            verify(targets[t],legacy,alpha,base,size,1,last);
    if(legacy&&!immutable)for(int alpha=0;alpha<2;++alpha)for(int base=0;base<=2;base+=2)
        verify(GL_TEXTURE_2D,legacy,alpha,base,4,0,2);
    if(immutable&&!legacy)immutable_edges();
    errors(legacy);
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);printf("mipmap adapter failures=%d\n",failures);return failures?1:0;
}
