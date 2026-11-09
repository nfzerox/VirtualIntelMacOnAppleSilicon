/* Legacy sLuminance mip policies: derive the oracle from actual base pixels. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../PassthroughOpenGL/passthrough-opengl-mipmap-host.h"
static unsigned failures,checks;
static int native_only,dense;
static void check(int ok,const char *name)
{printf("%s %s\n",ok?"PASS":"FAIL",name);fflush(stdout);++checks;failures+=!ok;}
static void define(GLenum target,GLenum image,int level,GLenum internal,int size,const void *pixels)
{
    if(target==GL_TEXTURE_1D)glTexImage1D(target,level,internal,size,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    else if(target==GL_TEXTURE_3D)glTexImage3D(target,level,internal,size,size,size,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    else glTexImage2D(image,level,internal,size,size,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
}
static unsigned mean(unsigned a,unsigned b,int decode)
{
    double x=a/255.0,y=b/255.0;
    if(decode){x=x<=.04045?x/12.92:pow((x+.055)/1.055,2.4);y=y<=.04045?y/12.92:pow((y+.055)/1.055,2.4);}
    double v=(x+y)/2;
    if(decode)v=v<.0031308?v*12.92:1.055*pow(v,1/2.4)-.055;
    return (unsigned)lround(v*255);
}
static void verify(GLenum target,GLenum internal,int skip,int base)
{
    unsigned before=failures;GLuint texture=0;glGenTextures(1,&texture);glBindTexture(target,texture);
    glTexParameteri(target,GL_TEXTURE_BASE_LEVEL,base);glTexParameteri(target,GL_TEXTURE_MAX_LEVEL,base+3);
    int faces=target==GL_TEXTURE_CUBE_MAP?6:1,height=target==GL_TEXTURE_1D?1:8,depth=target==GL_TEXTURE_3D?8:1;
    unsigned char source[2048],original[6][2048],out[2048];GLint original_internal[6]={0};
    if(dense&&base)for(int level=0;level<base;++level)for(int f=0;f<faces;++f)
        define(target,faces==6?GL_TEXTURE_CUBE_MAP_POSITIVE_X+f:target,level,internal,8<<(base-level),NULL);
    for(int face=0;face<faces;++face){
        for(int z=0;z<depth;++z)for(int y=0;y<height;++y)for(int x=0;x<8;++x){
            unsigned char *p=source+((z*height+y)*8+x)*4;
            p[0]=(unsigned char)((x&1?144:16)+face);p[1]=p[0];p[2]=p[0];p[3]=(unsigned char)(64+face);
        }
        GLenum image=faces==6?GL_TEXTURE_CUBE_MAP_POSITIVE_X+face:target;
        define(target,image,base,internal,8,source);
        glGetTexLevelParameteriv(image,base,GL_TEXTURE_INTERNAL_FORMAT,original_internal+face);
        glGetTexImage(image,base,GL_RGBA,GL_UNSIGNED_BYTE,original[face]);
    }
    GLenum error=glGetError();check(!error,"sLuminance base image and native readback");
    printf("case target=%x requested=%x actual=%x skip=%d base=%d dense=%d allocation_error=%x\n",
        target,internal,original_internal[0],skip,base,dense,error);
    if(error)goto done;
    glTexParameteri(target,GL_TEXTURE_BASE_LEVEL,base);glTexParameteri(target,GL_TEXTURE_MAX_LEVEL,base+3);
    glTexParameteri(target,GL_TEXTURE_SRGB_DECODE_EXT,skip?GL_SKIP_DECODE_EXT:GL_DECODE_EXT);
    error=glGetError();check(!error,"sLuminance decode state accepted");if(error)goto done;
    if(native_only){glGenerateMipmapEXT(target);error=glGetError();}
    else error=DGGenerateSrgbMipmap(target,1,0);
    if(error)printf("generation_error=%x\n",error);
    check(!error,"sLuminance mip chain generated");if(error)goto done;
    for(int face=0;face<faces;++face){
        GLenum image=faces==6?GL_TEXTURE_CUBE_MAP_POSITIVE_X+face:target;
        glGetTexImage(image,base,GL_RGBA,GL_UNSIGNED_BYTE,out);
        check(!glGetError()&&!memcmp(out,original[face],8*height*depth*4),"sLuminance base pixels unchanged");
        for(int level=1;level<=3;++level){
            int w=8>>level,h=target==GL_TEXTURE_1D?1:w,d=target==GL_TEXTURE_3D?w:1;
            memset(out,0xa5,sizeof(out));glGetTexImage(image,base+level,GL_RGBA,GL_UNSIGNED_BYTE,out);
            error=glGetError();check(!error,"sLuminance mip pixels readable");
            GLint value=0;glGetTexLevelParameteriv(image,base+level,GL_TEXTURE_INTERNAL_FORMAT,&value);
            check(!glGetError()&&value==original_internal[face],"sLuminance internal format preserved");
            unsigned expected[4];for(unsigned k=0;k<4;++k)expected[k]=mean(original[face][k],original[face][4+k],!skip&&k<3);
            int good=!error;for(int i=0;i<w*h*d&&good;++i)for(unsigned k=0;k<4;++k)
                if(abs((int)out[i*4+k]-(int)expected[k])>2){good=0;break;}
            if(!good)printf("pixel_mismatch level=%d face=%d actual=%u,%u,%u,%u expected=%u,%u,%u,%u\n",
                level,face,out[0],out[1],out[2],out[3],expected[0],expected[1],expected[2],expected[3]);
            check(good,"sLuminance mip color policy");
        }
    }
    GLint value=0;glGetTexParameteriv(target,GL_TEXTURE_BASE_LEVEL,&value);check(value==base,"sLuminance base level preserved");
    glGetTexParameteriv(target,GL_TEXTURE_MAX_LEVEL,&value);check(value==base+3,"sLuminance max level preserved");
    glGetTexParameteriv(target,GL_TEXTURE_SRGB_DECODE_EXT,&value);check(value==(skip?GL_SKIP_DECODE_EXT:GL_DECODE_EXT),"sLuminance decode policy preserved");
done:
    glDeleteTextures(1,&texture);
    printf("%s case target=%x format=%x skip=%d base=%d\n",before==failures?"PASS":"FAIL",target,internal,skip,base);
}
int main(int argc,char **argv)
{
    for(int i=1;i<argc;++i){if(!strcmp(argv[i],"--native")||!strcmp(argv[i],"--executor"))native_only=1;else if(!strcmp(argv[i],"--dense"))dense=1;else return 2;}
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;CGLDestroyPixelFormat(pf);
    printf("renderer=%s version=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    const GLenum targets[]={GL_TEXTURE_1D,GL_TEXTURE_2D,GL_TEXTURE_3D,GL_TEXTURE_CUBE_MAP};
    const GLenum formats[]={GL_SLUMINANCE8,GL_SLUMINANCE8_ALPHA8,GL_SLUMINANCE,GL_SLUMINANCE_ALPHA,
        GL_COMPRESSED_SLUMINANCE,GL_COMPRESSED_SLUMINANCE_ALPHA};
    for(unsigned t=0;t<4;++t)for(unsigned f=0;f<6;++f)for(int skip=0;skip<2;++skip)for(int base=0;base<=2;base+=2)
        verify(targets[t],formats[f],skip,base);
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);printf("sluminance checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
