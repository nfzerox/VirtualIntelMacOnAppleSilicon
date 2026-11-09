/* Public GL compressed mip generation and recompression controls. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include "../PassthroughOpenGL/passthrough-opengl-mipmap-host.h"
static unsigned failures,pixel_failures,metadata_failures;
static int dense_prefix,immutable_storage,linear_formats;
static void check(int good,const char *name)
{printf("%s %s\n",good?"PASS":"FAIL",name);failures+=!good;}
static void block(unsigned char *p,unsigned format,unsigned face)
{
    unsigned offset=format<2?0:8;
    memset(p,0,format<2?8:16);
    if(format==2)for(unsigned i=0;i<8;++i)p[i]=0xc8;
    if(format==3){
        p[0]=204;p[1]=136;
        unsigned long long bits=0;for(unsigned i=0;i<16;++i)bits|=(unsigned long long)(i&1)<<(3*i);
        for(unsigned i=0;i<6;++i)p[2+i]=(unsigned char)(bits>>(8*i));
    }
    unsigned high=0xc618+face*0x20,low=0x4208+face*0x20;
    p[offset]=(unsigned char)high;p[offset+1]=(unsigned char)(high>>8);
    p[offset+2]=(unsigned char)low;p[offset+3]=(unsigned char)(low>>8);
    for(unsigned i=0;i<4;++i)p[offset+4+i]=0x44;
}
static unsigned encoded_mean(const unsigned char *pixels,unsigned count,unsigned channel,int skip)
{
    double total=0;
    for(unsigned i=0;i<count;++i){double v=pixels[4*i+channel]/255.0;
        if(!skip&&channel<3)v=v<=.04045?v/12.92:pow((v+.055)/1.055,2.4);
        total+=v;}
    double v=total/count;
    if(!skip&&channel<3)v=v<.0031308?v*12.92:1.055*pow(v,1/2.4)-.055;
    return (unsigned)lround(v*255);
}
static void verify(unsigned format,int skip,int base,unsigned kind,int adapter,int legacy)
{
    const GLenum targets[]={GL_TEXTURE_2D,GL_TEXTURE_CUBE_MAP,0x8c1a,0x9009};
    GLenum internal=(linear_formats?0x83f0:0x8c4c)+format,target=targets[kind];
    int cube=kind==1,array=kind>=2;
    unsigned depth=array?(kind==3?12:3):1,faces=cube?6:1;
    GLuint texture=0;glGenTextures(1,&texture);glBindTexture(target,texture);
    unsigned char blocks[768],input[6][3072],output[3072];
    unsigned stride=format<2?8:16;
    if(immutable_storage){
        typedef void (*Storage2)(GLenum,GLsizei,GLenum,GLsizei,GLsizei);
        typedef void (*Storage3)(GLenum,GLsizei,GLenum,GLsizei,GLsizei,GLsizei);
        Storage2 two=(Storage2)dlsym(RTLD_DEFAULT,"glTexStorage2D");Storage3 three=(Storage3)dlsym(RTLD_DEFAULT,"glTexStorage3D");
        check(two&&three,"compressed immutable storage API available");if(!two||!three)goto done;
        if(array)three(target,base+4,internal,8<<base,8<<base,depth);
        else two(target,base+4,internal,8<<base,8<<base);
        check(!glGetError(),"compressed immutable storage allocated");
    }
    if(dense_prefix&&base){
        for(int level=0;level<base;++level){
            unsigned size=8u<<(base-level),count=(size/4)*(size/4);
            unsigned char *prefix=malloc(count*depth*stride);
            if(!prefix){check(0,"prefix allocation");goto done;}
            for(unsigned face=0;face<faces;++face){
                for(unsigned z=0;z<depth;++z)for(unsigned b=0;b<count;++b)
                    block(prefix+(z*count+b)*stride,format,cube?face:z);
                if(immutable_storage){
                    if(array)glCompressedTexSubImage3D(target,level,0,0,0,size,size,depth,internal,count*depth*stride,prefix);
                    else glCompressedTexSubImage2D(cube?GL_TEXTURE_CUBE_MAP_POSITIVE_X+face:target,level,0,0,size,size,internal,count*stride,prefix);
                }
                else if(array)glCompressedTexImage3D(target,level,internal,size,size,depth,0,count*depth*stride,prefix);
                else glCompressedTexImage2D(cube?GL_TEXTURE_CUBE_MAP_POSITIVE_X+face:target,
                    level,internal,size,size,0,count*stride,prefix);
            }
            free(prefix);
        }
    }
    for(unsigned i=0;i<faces;++i){
        for(unsigned z=0;z<depth;++z)for(unsigned j=0;j<4;++j)
            block(blocks+(4*z+j)*stride,format,cube?i:z);
        if(immutable_storage){
            if(array)glCompressedTexSubImage3D(target,base,0,0,0,8,8,depth,internal,4*stride*depth,blocks);
            else glCompressedTexSubImage2D(cube?GL_TEXTURE_CUBE_MAP_POSITIVE_X+i:target,base,0,0,8,8,internal,4*stride,blocks);
        }
        else if(array)glCompressedTexImage3D(target,base,internal,8,8,depth,0,4*stride*depth,blocks);
        else glCompressedTexImage2D(cube?GL_TEXTURE_CUBE_MAP_POSITIVE_X+i:target,
            base,internal,8,8,0,4*stride,blocks);
    }
    glTexParameteri(target,GL_TEXTURE_BASE_LEVEL,base);glTexParameteri(target,GL_TEXTURE_MAX_LEVEL,base+3);
    glTexParameteri(target,GL_TEXTURE_SRGB_DECODE_EXT,skip?GL_SKIP_DECODE_EXT:GL_DECODE_EXT);
    for(unsigned i=0;i<faces;++i)
        glGetTexImage(cube?GL_TEXTURE_CUBE_MAP_POSITIVE_X+i:target,base,GL_RGBA,GL_UNSIGNED_BYTE,input[i]);
    GLenum error=glGetError();check(!error,"compressed source allocated and read");
    printf("case target=%x format=%x skip=%d base=%d allocation_error=%x\n",target,internal,skip,base,error);
    if(error)goto done;
    if(adapter)error=DGGenerateSrgbMipmap(target,legacy,0);
    else glGenerateMipmapEXT(target);
    if(!error)error=glGetError();check(!error,"compressed mip chain generated");
    if(error)goto done;
    for(unsigned face=0;face<faces;++face){
        GLenum image=cube?GL_TEXTURE_CUBE_MAP_POSITIVE_X+face:target;
        memset(output,0,sizeof(output));glGetTexImage(image,base,GL_RGBA,GL_UNSIGNED_BYTE,output);
        check(!glGetError()&&!memcmp(output,input[face],256*depth),"base pixels unchanged");
        for(unsigned level=1;level<=3;++level){
            unsigned size=8>>level;GLint value=0;
            glGetTexLevelParameteriv(image,base+level,GL_TEXTURE_INTERNAL_FORMAT,&value);
            int good=!glGetError()&&value==(GLint)internal;
            metadata_failures+=!good;check(good,"compressed internal format preserved");
            glGetTexLevelParameteriv(image,base+level,GL_TEXTURE_COMPRESSED,&value);
            error=glGetError();good=!error&&value==GL_TRUE;
            if(!good)printf("compressed-query value=%d error=%x target=%x format=%x base=%d level=%u\n",value,error,target,internal,base,level);
            metadata_failures+=!good;check(good,"child storage remains compressed");
            glGetTexLevelParameteriv(image,base+level,GL_TEXTURE_COMPRESSED_IMAGE_SIZE,&value);
            error=glGetError();good=!error&&value==(GLint)(stride*depth);
            if(!good)printf("size-query value=%d error=%x target=%x format=%x base=%d level=%u\n",value,error,target,internal,base,level);
            metadata_failures+=!good;check(good,"child compressed byte count preserved");
            memset(output,0,sizeof(output));glGetTexImage(image,base+level,GL_RGBA,GL_UNSIGNED_BYTE,output);
            error=glGetError();
            for(unsigned z=0;z<depth;++z){good=!error;unsigned expected[4];
                const unsigned char *pixels=output+z*size*size*4;
                /* Allow block-encoding quantization across three levels:
                 * eight byte units for RGB, nine for four-bit DXT3 alpha. */
                for(unsigned channel=0;channel<4;++channel){expected[channel]=encoded_mean(input[face]+z*256,64,channel,skip||linear_formats);
                    for(unsigned pixel=0;pixel<size*size;++pixel)
                        if(abs((int)pixels[4*pixel+channel]-(int)expected[channel])>(channel==3?9:8))good=0;}
                printf("target=%x format=%x skip=%d base=%d face=%u layer=%u level=%u value=%u,%u,%u,%u expected=%u,%u,%u,%u error=%x\n",
                    target,internal,skip,base,face,z,level,pixels[0],pixels[1],pixels[2],pixels[3],expected[0],expected[1],expected[2],expected[3],error);
                pixel_failures+=!good;check(good,"compressed mip pixels match source-space filtering");
            }
        }
    }
    /* Separately determine whether native public GL accepts RGBA updates
     * into compressed storage. This is not a replacement for the mip check. */
    for(unsigned i=0;i<16*depth;++i){output[4*i]=128;output[4*i+1]=128;output[4*i+2]=128;output[4*i+3]=170;}
    GLenum image=cube?GL_TEXTURE_CUBE_MAP_POSITIVE_X:target;
    if(array)glTexSubImage3D(image,base+1,0,0,0,4,4,depth,GL_RGBA,GL_UNSIGNED_BYTE,output);
    else glTexSubImage2D(image,base+1,0,0,4,4,GL_RGBA,GL_UNSIGNED_BYTE,output);
    error=glGetError();printf("rgba-to-compressed error=%x format=%x\n",error,internal);
    check(!error,"public RGBA update accepted");
    if(!error){glGetTexImage(image,base+1,GL_RGBA,GL_UNSIGNED_BYTE,input[0]);error=glGetError();
        int good=!error&&abs((int)input[0][0]-128)<=8&&abs((int)input[0][1]-128)<=8&&abs((int)input[0][2]-128)<=8;
        printf("recompressed=%u,%u,%u,%u error=%x\n",input[0][0],input[0][1],input[0][2],input[0][3],error);
        check(good,"public RGBA update recompresses RGB");}
done:
    glDeleteTextures(1,&texture);
}
int main(int argc,char **argv)
{
    int adapter=0,legacy=0;
    for(int i=1;i<argc;++i){if(!strcmp(argv[i],"--adapter"))adapter=1;
        else if(!strcmp(argv[i],"--legacy"))legacy=1;
        else if(!strcmp(argv[i],"--immutable"))immutable_storage=1;
        else if(!strcmp(argv[i],"--linear"))linear_formats=1;
        else if(!strcmp(argv[i],"--dense"))dense_prefix=1;else return 2;}
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)(legacy?kCGLOGLPVersion_Legacy:kCGLOGLPVersion_GL4_Core),0};
    CGLPixelFormatObj pf=NULL;CGLContextObj context=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attrs,&pf,&count)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&context)||CGLSetCurrentContext(context))return 2;CGLDestroyPixelFormat(pf);
    printf("compressed mip renderer=%s version=%s legacy=%d adapter=%d dense=%d\n",glGetString(GL_RENDERER),glGetString(GL_VERSION),legacy,adapter,dense_prefix);
    for(unsigned format=0;format<4;++format)for(int skip=0;skip<2;++skip)
        for(int base=0;base<=2;base+=2)for(unsigned kind=0;kind<(legacy?2u:4u);++kind)
            verify(format,skip,base,kind,adapter,legacy);
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);
    printf("compressed mip failures=%u pixel_failures=%u metadata_failures=%u\n",failures,pixel_failures,metadata_failures);return failures?1:0;
}
