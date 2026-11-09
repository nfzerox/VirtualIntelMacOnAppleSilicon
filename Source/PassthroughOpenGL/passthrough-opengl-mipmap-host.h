/* Public GL color-policy and nonzero-base mip corrections. Proxy generation
 * preserves original base/other levels; no private renderer access or
 * CPU-mapped pixel conversion is used here. */
#ifndef VIM_PASSTHROUGH_OPENGL_MIPMAP_HOST_H
#define VIM_PASSTHROUGH_OPENGL_MIPMAP_HOST_H
#include <string.h>
#include <stdint.h>
#include "passthrough-opengl-mipmap-execute.h"

struct DGMipmapTransfer {
    GLfloat values[24],matrix[16];GLint map,mode;GLboolean enabled[8];
    int legacy,imaging;
};
static const GLenum dg_mipmap_transfer_keys[]={
    GL_RED_SCALE,GL_GREEN_SCALE,GL_BLUE_SCALE,GL_ALPHA_SCALE,
    GL_RED_BIAS,GL_GREEN_BIAS,GL_BLUE_BIAS,GL_ALPHA_BIAS,
    GL_POST_CONVOLUTION_RED_SCALE,GL_POST_CONVOLUTION_GREEN_SCALE,
    GL_POST_CONVOLUTION_BLUE_SCALE,GL_POST_CONVOLUTION_ALPHA_SCALE,
    GL_POST_CONVOLUTION_RED_BIAS,GL_POST_CONVOLUTION_GREEN_BIAS,
    GL_POST_CONVOLUTION_BLUE_BIAS,GL_POST_CONVOLUTION_ALPHA_BIAS,
    GL_POST_COLOR_MATRIX_RED_SCALE,GL_POST_COLOR_MATRIX_GREEN_SCALE,
    GL_POST_COLOR_MATRIX_BLUE_SCALE,GL_POST_COLOR_MATRIX_ALPHA_SCALE,
    GL_POST_COLOR_MATRIX_RED_BIAS,GL_POST_COLOR_MATRIX_GREEN_BIAS,
    GL_POST_COLOR_MATRIX_BLUE_BIAS,GL_POST_COLOR_MATRIX_ALPHA_BIAS};
static const GLenum dg_mipmap_imaging_enables[]={GL_CONVOLUTION_1D,GL_CONVOLUTION_2D,
    GL_SEPARABLE_2D,GL_COLOR_TABLE,GL_POST_CONVOLUTION_COLOR_TABLE,
    GL_POST_COLOR_MATRIX_COLOR_TABLE,GL_HISTOGRAM,GL_MINMAX};
static int DGMipmapImaging(void)
{
    const char *all=(const char *)glGetString(GL_EXTENSIONS),*name="GL_ARB_imaging";
    size_t n=strlen(name);
    for(const char *p=all;p&&(p=strstr(p,name));p+=n)
        if((p==all||p[-1]==' ')&&(!p[n]||p[n]==' '))return 1;
    return 0;
}
static void DGMipmapCanonicalTransfer(struct DGMipmapTransfer *saved,int legacy)
{
    memset(saved,0,sizeof(*saved));saved->legacy=legacy;if(!legacy)return;
    saved->imaging=DGMipmapImaging();
    for(unsigned i=0;i<(saved->imaging?24u:8u);++i){
        GLfloat value=(i%8)<4?1:0;glGetFloatv(dg_mipmap_transfer_keys[i],saved->values+i);
        if(saved->values[i]!=value)glPixelTransferf(dg_mipmap_transfer_keys[i],value);
    }
    glGetIntegerv(GL_MAP_COLOR,&saved->map);if(saved->map)glPixelTransferi(GL_MAP_COLOR,GL_FALSE);
    if(saved->imaging){
        for(unsigned i=0;i<8;++i){saved->enabled[i]=glIsEnabled(dg_mipmap_imaging_enables[i]);
            if(saved->enabled[i])glDisable(dg_mipmap_imaging_enables[i]);}
        glGetFloatv(GL_COLOR_MATRIX,saved->matrix);glGetIntegerv(GL_MATRIX_MODE,&saved->mode);
        glMatrixMode(GL_COLOR);glLoadIdentity();glMatrixMode((GLenum)saved->mode);
    }
}
static void DGMipmapRestoreTransfer(const struct DGMipmapTransfer *saved)
{
    if(!saved->legacy)return;
    for(unsigned i=0;i<(saved->imaging?24u:8u);++i)
        if(saved->values[i]!=((i%8)<4?1:0))glPixelTransferf(dg_mipmap_transfer_keys[i],saved->values[i]);
    if(saved->map)glPixelTransferi(GL_MAP_COLOR,saved->map);
    if(saved->imaging){
        glMatrixMode(GL_COLOR);glLoadMatrixf(saved->matrix);glMatrixMode((GLenum)saved->mode);
        for(unsigned i=0;i<8;++i)if(saved->enabled[i])glEnable(dg_mipmap_imaging_enables[i]);
    }
}
static GLenum DGMipmapBinding(GLenum target)
{
    switch(target){
    case GL_TEXTURE_1D:return GL_TEXTURE_BINDING_1D;
    case GL_TEXTURE_2D:return GL_TEXTURE_BINDING_2D;
    case GL_TEXTURE_3D:return GL_TEXTURE_BINDING_3D;
    case GL_TEXTURE_CUBE_MAP:return GL_TEXTURE_BINDING_CUBE_MAP;
    case 0x8c18:return 0x8c1c; /* TEXTURE_1D_ARRAY / BINDING */
    case 0x8c1a:return 0x8c1d; /* TEXTURE_2D_ARRAY / BINDING */
    case 0x9009:return 0x900a; /* TEXTURE_CUBE_MAP_ARRAY / BINDING */
    default:return 0;
    }
}
static void DGMipmapAttach(GLenum target,GLuint texture,GLint level,GLint layer)
{
    const GLenum read=GL_READ_FRAMEBUFFER_EXT,attachment=GL_COLOR_ATTACHMENT0_EXT;
    if(target==GL_TEXTURE_1D)glFramebufferTexture1DEXT(read,attachment,target,texture,level);
    else if(target==GL_TEXTURE_2D||target==GL_TEXTURE_CUBE_MAP)
        glFramebufferTexture2DEXT(read,attachment,target==GL_TEXTURE_CUBE_MAP?
            GL_TEXTURE_CUBE_MAP_POSITIVE_X+(GLenum)layer:target,texture,level);
    else glFramebufferTextureLayerEXT(read,attachment,texture,level,layer);
}
static void DGMipmapCopy(GLenum target,GLint level,GLint layer,GLsizei width,GLsizei height)
{
    if(target==GL_TEXTURE_1D)glCopyTexSubImage1D(target,level,0,0,0,width);
    else if(target==GL_TEXTURE_2D||target==GL_TEXTURE_CUBE_MAP)
        glCopyTexSubImage2D(target==GL_TEXTURE_CUBE_MAP?GL_TEXTURE_CUBE_MAP_POSITIVE_X+(GLenum)layer:target,
            level,0,0,0,0,width,height);
    else if(target==0x8c18)glCopyTexSubImage2D(target,level,0,layer,0,0,width,1);
    else glCopyTexSubImage3D(target,level,0,0,layer,0,0,width,height);
}
/* Compressed images and sparse nonzero-base textures cannot be read FBOs.
 * Keep those transfers in GPU buffer storage, with no mapped/read CPU pixels.
 * The same path permits the compatibility profile's unnamed texture object. */
static GLenum DGMipmapBufferCopy(GLenum target,GLuint source,GLuint destination,
    GLint from,GLint to,GLsizei width,GLsizei height,GLsizei depth,GLenum type)
{
    for(unsigned face=0;face<(target==GL_TEXTURE_CUBE_MAP?6u:1u);++face){
        GLenum image=target==GL_TEXTURE_CUBE_MAP?GL_TEXTURE_CUBE_MAP_POSITIVE_X+face:target;
        glBindTexture(target,source);glGetTexImage(image,from,GL_RGBA,type,NULL);
        glBindTexture(target,destination);
        if(target==GL_TEXTURE_1D)glTexSubImage1D(image,to,0,width,GL_RGBA,type,NULL);
        else if(target==GL_TEXTURE_2D||target==GL_TEXTURE_CUBE_MAP||target==0x8c18)
            glTexSubImage2D(image,to,0,0,width,height,GL_RGBA,type,NULL);
        else glTexSubImage3D(image,to,0,0,0,width,height,depth,GL_RGBA,type,NULL);
        GLenum error=glGetError();if(error)return error;
    }
    return GL_NO_ERROR;
}
static const GLenum dg_mipmap_pixel_keys[]={
    GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,GL_PACK_SKIP_ROWS,GL_PACK_SKIP_PIXELS,
    GL_PACK_IMAGE_HEIGHT,GL_PACK_SKIP_IMAGES,GL_PACK_SWAP_BYTES,GL_PACK_LSB_FIRST,
    GL_PACK_ROW_BYTES_APPLE,
    GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_PIXELS,
    GL_UNPACK_IMAGE_HEIGHT,GL_UNPACK_SKIP_IMAGES,GL_UNPACK_SWAP_BYTES,GL_UNPACK_LSB_FIRST,
    GL_UNPACK_ROW_BYTES_APPLE};
static GLenum DGMipmapNativeGenerate(GLenum target,GLenum prior)
{glGenerateMipmapEXT(target);GLenum error=glGetError();return prior?prior:error;}
/* ARB_texture_storage clamps the effective base/max to allocated levels;
 * the queryable user parameters themselves must stay unchanged. */
static GLint DGMipmapImmutableRange(GLenum target,GLenum face,GLint *base,GLint *maximum)
{
    GLint width=0,height=0,depth=0;
    glGetTexLevelParameteriv(face,0,GL_TEXTURE_WIDTH,&width);
    glGetTexLevelParameteriv(face,0,GL_TEXTURE_HEIGHT,&height);
    glGetTexLevelParameteriv(face,0,GL_TEXTURE_DEPTH,&depth);
    GLint spatial=width;
    if(target!=0x8c18 && height>spatial)spatial=height;
    if(target==GL_TEXTURE_3D && depth>spatial)spatial=depth;
    GLint last=0;
    while(spatial>1){
        GLint allocated=0;glGetTexLevelParameteriv(face,last+1,GL_TEXTURE_WIDTH,&allocated);
        if(!allocated)break;++last;spatial/=2;
    }
    if(*base>last)*base=last;
    if(*maximum>last)*maximum=last;
    if(*maximum<*base)*maximum=*base;
    return last;
}
static GLenum DGMipmapValidateCube(GLenum target,GLint base,GLint maximum)
{
    /* Native per-face format queries can hide inconsistent definitions.
     * Its no-child generation still checks true cube completeness without
     * running the destructive filter. Preserve the real native error. */
    glTexParameteri(target,GL_TEXTURE_MAX_LEVEL,base);
    GLenum error=glGetError();if(!error)error=DGMipmapNativeGenerate(target,GL_NO_ERROR);
    glTexParameteri(target,GL_TEXTURE_MAX_LEVEL,maximum);
    GLenum restored=glGetError();return error?error:restored;
}
static void DGMipmapAllocate(GLenum target,GLint level,GLint internal,GLsizei width,GLsizei height,GLsizei depth)
{
    if(target==GL_TEXTURE_1D)glTexImage1D(target,level,internal,width,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    else if(target==GL_TEXTURE_CUBE_MAP)for(unsigned f=0;f<6;++f)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+f,level,internal,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    else if(target==GL_TEXTURE_2D || target==0x8c18)
        glTexImage2D(target,level,internal,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    else glTexImage3D(target,level,internal,width,height,depth,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
}
static GLenum DGGenerateSrgbMipmap(GLenum target,int legacy,int inside_begin)
{
    const char *version=inside_begin?NULL:(const char *)glGetString(GL_VERSION);
    GLenum binding=DGMipmapBinding(target);
    if(!version||!strstr(version,"Metal")||!binding){glGenerateMipmapEXT(target);return GL_NO_ERROR;}
    GLenum prior=glGetError(),error=GL_NO_ERROR;
    GLint base=0,maximum=0,internal=0,mode=0,texture=0,width=0,height=1,depth=1;
    GLenum face=target==GL_TEXTURE_CUBE_MAP?GL_TEXTURE_CUBE_MAP_POSITIVE_X:target;
    glGetTexParameteriv(target,GL_TEXTURE_BASE_LEVEL,&base);
    GLint immutable=0;
    if(!legacy)glGetTexParameteriv(target,0x912f,&immutable); /* TEXTURE_IMMUTABLE_FORMAT */
    glGetTexParameteriv(target,GL_TEXTURE_MAX_LEVEL,&maximum);
    GLint allocated_last=immutable?DGMipmapImmutableRange(target,face,&base,&maximum):-1;
    /* Metal blits can overwrite immutable levels above TEXTURE_MAX_LEVEL,
     * even with base zero. Generate only the requested proxy range rather
     * than damaging the excluded tail and trying to repair it afterward. */
    int range_proxy=base||(immutable&&maximum<allocated_last);
    glGetTexLevelParameteriv(face,base,GL_TEXTURE_INTERNAL_FORMAT,&internal);
    int srgb_compressed=internal>=0x8c4c&&internal<=0x8c4f;
    int linear_compressed=internal>=0x83f0&&internal<=0x83f3; /* S3TC DXT1/3/5 */
    int compressed=srgb_compressed||linear_compressed;
    int luminance=internal==GL_SLUMINANCE8||internal==GL_SLUMINANCE8_ALPHA8;
    int floating=internal==GL_RGB16F_ARB||internal==GL_RGBA16F_ARB||internal==GL_R16F||internal==GL_RG16F;
    int unorm16=internal==GL_R16||internal==GL_RG16||internal==GL_RGB16||internal==GL_RGBA16;
    int word_color=floating||unorm16;
    int linear=internal==GL_RGB8||internal==GL_RGBA8||internal==GL_R8||internal==GL_RG8||word_color||linear_compressed;
    if(internal!=GL_SRGB8&&internal!=GL_SRGB8_ALPHA8&&!compressed&&!luminance&&!(range_proxy&&linear)){
        glGetError();return DGMipmapNativeGenerate(target,prior);
    }
    if(!linear)glGetTexParameteriv(target,GL_TEXTURE_SRGB_DECODE_EXT,&mode);
    if(glGetError())return DGMipmapNativeGenerate(target,prior);
    /* Compressed and sLuminance paths already implement SKIP_DECODE, but
     * omit conversion for DECODE. Ordinary sRGB has the opposite bug. */
    int encoded_proxy=srgb_compressed||luminance;
    if(!range_proxy && (encoded_proxy?mode!=GL_DECODE_EXT:mode!=GL_SKIP_DECODE_EXT))
        return DGMipmapNativeGenerate(target,prior);
    if(range_proxy)encoded_proxy=!linear&&mode==GL_DECODE_EXT;
    /* Nonzero bases and clipped immutable ranges must not run the destructive
     * native generator. Immutable storage guarantees completeness; mutable
     * storage is validated below, then its children are defined only after
     * the proxy is generated. */
    if(!range_proxy){error=DGMipmapNativeGenerate(target,GL_NO_ERROR);if(error)return prior?prior:error;}
    glGetIntegerv(binding,&texture);
    glGetTexLevelParameteriv(face,base,GL_TEXTURE_WIDTH,&width);
    glGetTexLevelParameteriv(face,base,GL_TEXTURE_HEIGHT,&height);
    glGetTexLevelParameteriv(face,base,GL_TEXTURE_DEPTH,&depth);
    int array1=target==0x8c18,array2=target==0x8c1a||target==0x9009;
    int cube=target==GL_TEXTURE_CUBE_MAP,volume=target==GL_TEXTURE_3D;
    int allocate=base&&!immutable;
    if(allocate){
        GLint border=0;if(legacy)glGetTexLevelParameteriv(face,base,GL_TEXTURE_BORDER,&border);
        if(glGetError() || !width || !height || !depth || border)return DGMipmapNativeGenerate(target,prior);
        if(cube || target==0x9009){
            error=DGMipmapValidateCube(target,base,maximum);if(error)return prior?prior:error;
        }
    }
    GLint spatial=width;if(!array1&&height>spatial)spatial=height;
    if(volume&&depth>spatial)spatial=depth;
    GLint levels=0;
    while(spatial>1&&levels<maximum-base){
        if(!allocate){GLint allocated=0;glGetTexLevelParameteriv(face,base+levels+1,GL_TEXTURE_WIDTH,&allocated);
            if(!allocated)break;}
        ++levels;spatial/=2;
    }
    if(!levels)return prior;
    GLint read=0,unpack=0;glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&read);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpack);
    GLboolean srgb=glIsEnabled(GL_FRAMEBUFFER_SRGB_EXT);
    struct DGMipmapTransfer transfer;DGMipmapCanonicalTransfer(&transfer,legacy);
    GLuint proxy=0,fbo=0,buffer=0;GLint pack=0,pixels[18];
    int buffered=compressed||luminance||(base!=0&&!immutable)||texture==0,pixel_state_saved=0;
    /* Preserve HDR range / normalized 16-bit precision. An RGBA8 proxy/byte
     * PBO would silently clamp or quantize a successful generation. */
    GLint proxy_format=word_color?internal:encoded_proxy?GL_SRGB8_ALPHA8:GL_RGBA8;
    GLenum transfer_type=floating?GL_HALF_FLOAT_ARB:unorm16?GL_UNSIGNED_SHORT:GL_UNSIGNED_BYTE;
    glGenTextures(1,&proxy);glGenFramebuffersEXT(1,&fbo);
    glBindTexture(target,proxy);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    if(target==GL_TEXTURE_1D)glTexImage1D(target,0,proxy_format,width,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    else if(target==GL_TEXTURE_2D||array1)
        glTexImage2D(target,0,proxy_format,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    else if(cube)for(unsigned i=0;i<6;++i)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+i,0,proxy_format,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    else glTexImage3D(target,0,proxy_format,width,height,depth,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glTexParameteri(target,GL_TEXTURE_MAX_LEVEL,levels);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,fbo);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
    glDisable(GL_FRAMEBUFFER_SRGB_EXT);
    error=glGetError();
    if(buffered&&!error){
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&pack);
        for(unsigned i=0;i<18;++i){glGetIntegerv(dg_mipmap_pixel_keys[i],pixels+i);
            glPixelStorei(dg_mipmap_pixel_keys[i],i%9==0?1:0);}
        pixel_state_saved=1;
        glGenBuffers(1,&buffer);glBindBuffer(GL_PIXEL_PACK_BUFFER,buffer);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,buffer);
        uint64_t bytes=(uint64_t)width*(uint64_t)height*(uint64_t)depth*4*(word_color?sizeof(uint16_t):1);
        if(bytes>(uint64_t)INTPTR_MAX)error=GL_OUT_OF_MEMORY;
        else {glBufferData(GL_PIXEL_PACK_BUFFER,(GLsizeiptr)bytes,NULL,GL_STREAM_COPY);error=glGetError();}
        if(!error)error=DGMipmapBufferCopy(target,(GLuint)texture,proxy,base,0,width,height,depth,transfer_type);
    }
    GLint layers=cube?6:array1?height:(volume||array2)?depth:1;
    for(GLint i=0;!buffered&&!error&&i<layers;++i){
        DGMipmapAttach(target,(GLuint)texture,base,i);
        if(glCheckFramebufferStatusEXT(GL_READ_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT){error=GL_INVALID_OPERATION;break;}
        DGMipmapCopy(target,0,i,width,array1?1:height);error=glGetError();
    }
    if(!error){glGenerateMipmapEXT(target);error=glGetError();}
    glBindTexture(target,(GLuint)texture);
    for(GLint level=1;!error&&level<=levels;++level){
        width=width>1?width/2:1;
        if(!array1)height=height>1?height/2:1;
        if(volume)layers=layers>1?layers/2:1;
        if(allocate){
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
            DGMipmapAllocate(target,base+level,internal,width,height,volume?layers:depth);
            error=glGetError();if(buffered)glBindBuffer(GL_PIXEL_UNPACK_BUFFER,buffer);
            if(error)break;
        }
        if(buffered)error=DGMipmapBufferCopy(target,proxy,(GLuint)texture,level,base+level,
            width,height,volume?layers:depth,transfer_type);
        for(GLint i=0;!buffered&&!error&&i<layers;++i){
            DGMipmapAttach(target,proxy,level,i);
            if(glCheckFramebufferStatusEXT(GL_READ_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT){error=GL_INVALID_OPERATION;break;}
            DGMipmapCopy(target,base+level,i,width,array1?1:height);error=glGetError();
        }
    }
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,(GLuint)read);
    glBindTexture(target,(GLuint)texture);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,(GLuint)unpack);
    if(pixel_state_saved){
        glBindBuffer(GL_PIXEL_PACK_BUFFER,(GLuint)pack);
        for(unsigned i=0;i<18;++i)glPixelStorei(dg_mipmap_pixel_keys[i],pixels[i]);
        glDeleteBuffers(1,&buffer);
    }
    if(srgb)glEnable(GL_FRAMEBUFFER_SRGB_EXT);
    DGMipmapRestoreTransfer(&transfer);
    glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&proxy);
    GLenum restore_error=glGetError();return prior?prior:error?error:restore_error;
}
/* Automatic cube updates derive one face, even if the other five faces are
 * incomplete or have deliberately different child images. Explicit cube
 * GenerateMipmap would wrongly replace those children. Use a 2D native proxy
 * for this face only; all image transfers remain in an unmapped pixel buffer. */
static GLenum DGGenerateAutomaticCubeMipmap(GLenum face,GLint base,GLint last)
{
    GLint width=0,height=0,internal=0,depth=0,old_texture=0,pack=0,unpack=0,pixels[18];
    glGetTexLevelParameteriv(face,base,GL_TEXTURE_WIDTH,&width);
    glGetTexLevelParameteriv(face,base,GL_TEXTURE_HEIGHT,&height);
    glGetTexLevelParameteriv(face,base,GL_TEXTURE_INTERNAL_FORMAT,&internal);
    glGetTexLevelParameteriv(face,base,GL_TEXTURE_DEPTH_SIZE,&depth);
    GLenum format=GL_RGBA,type=GL_UNSIGNED_BYTE;unsigned pixel_bytes=4;
    if(depth){
        /* A normalized32 depth image has more precision than binary32. */
        format=GL_DEPTH_COMPONENT;type=internal==0x8cac?GL_FLOAT:GL_UNSIGNED_INT;pixel_bytes=4;
        if(internal==0x88f0||internal==0x8cad){ /* DEPTH24_STENCIL8 / DEPTH32F_STENCIL8 */
            format=GL_DEPTH_STENCIL;type=internal==0x88f0?GL_UNSIGNED_INT_24_8:0x8dad;
            pixel_bytes=internal==0x88f0?4:8;
        }
    }else{
        const GLenum sizes[]={GL_TEXTURE_RED_SIZE,GL_TEXTURE_GREEN_SIZE,GL_TEXTURE_BLUE_SIZE,
            GL_TEXTURE_ALPHA_SIZE,GL_TEXTURE_LUMINANCE_SIZE,GL_TEXTURE_INTENSITY_SIZE};
        for(unsigned i=0;i<6;++i){
            GLint bits=0,component=0;glGetTexLevelParameteriv(face,base,sizes[i],&bits);
            glGetTexLevelParameteriv(face,base,0x8c10+i,&component); /* component TYPE_ARB */
            if(component==GL_INT||component==GL_UNSIGNED_INT){format=0x8d99;type=(GLenum)component;pixel_bytes=16;}
            else if(bits>8&&format==GL_RGBA){type=GL_FLOAT;pixel_bytes=16;}
        }
    }
    GLenum error=glGetError();if(error)return error;
    if(width<1||height<1||last<=base)return GL_NO_ERROR;
    uint64_t bytes=(uint64_t)width*(uint64_t)height*pixel_bytes;
    if(bytes>(uint64_t)INTPTR_MAX)return GL_OUT_OF_MEMORY;
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&old_texture);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&pack);glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpack);
    for(unsigned i=0;i<18;++i){glGetIntegerv(dg_mipmap_pixel_keys[i],pixels+i);
        glPixelStorei(dg_mipmap_pixel_keys[i],i%9==0?1:0);}
    struct DGMipmapTransfer transfer;DGMipmapCanonicalTransfer(&transfer,1);
    GLfloat depth_scale=1,depth_bias=0;
    if(depth){glGetFloatv(GL_DEPTH_SCALE,&depth_scale);glGetFloatv(GL_DEPTH_BIAS,&depth_bias);
        glPixelTransferf(GL_DEPTH_SCALE,1);glPixelTransferf(GL_DEPTH_BIAS,0);}
    GLuint proxy=0,buffer=0;glGenTextures(1,&proxy);glGenBuffers(1,&buffer);
    glBindTexture(GL_TEXTURE_2D,proxy);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    glTexImage2D(GL_TEXTURE_2D,0,internal,width,height,0,format,type,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,last-base);
    GLint decode=GL_DECODE_EXT;glGetTexParameteriv(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_SRGB_DECODE_EXT,&decode);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_SRGB_DECODE_EXT,decode);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,buffer);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,buffer);
    glBufferData(GL_PIXEL_PACK_BUFFER,(GLsizeiptr)bytes,NULL,GL_STREAM_COPY);
    error=glGetError();
    if(!error){glGetTexImage(face,base,format,type,NULL);
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width,height,format,type,NULL);error=glGetError();}
    if(!error)error=DGGenerateSrgbMipmap(GL_TEXTURE_2D,1,0);
    for(GLint level=1;!error&&level<=last-base;++level){
        width=width>1?width/2:1;height=height>1?height/2:1;
        glGetTexImage(GL_TEXTURE_2D,level,format,type,NULL);error=glGetError();if(error)break;
        GLint old_width=0,old_height=0,old_internal=0,old_border=0;
        glGetTexLevelParameteriv(face,base+level,GL_TEXTURE_WIDTH,&old_width);
        glGetTexLevelParameteriv(face,base+level,GL_TEXTURE_HEIGHT,&old_height);
        glGetTexLevelParameteriv(face,base+level,GL_TEXTURE_INTERNAL_FORMAT,&old_internal);
        glGetTexLevelParameteriv(face,base+level,GL_TEXTURE_BORDER,&old_border);
        if(old_width!=width||old_height!=height||old_internal!=internal||old_border){
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
            glTexImage2D(face,base+level,internal,width,height,0,format,type,NULL);
        }
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,buffer);
        glTexSubImage2D(face,base+level,0,0,width,height,format,type,NULL);error=glGetError();
    }
    glBindTexture(GL_TEXTURE_2D,(GLuint)old_texture);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,(GLuint)pack);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,(GLuint)unpack);
    for(unsigned i=0;i<18;++i)glPixelStorei(dg_mipmap_pixel_keys[i],pixels[i]);
    if(depth){glPixelTransferf(GL_DEPTH_SCALE,depth_scale);glPixelTransferf(GL_DEPTH_BIAS,depth_bias);}
    DGMipmapRestoreTransfer(&transfer);glDeleteTextures(1,&proxy);glDeleteBuffers(1,&buffer);
    GLenum restored=glGetError();return error?error:restored;
}
static GLenum DGGenerateAutomaticCubeMipmapExecute(GLenum face,GLint base,GLint last,
    const struct DGMipmapExecution *execution)
{
    if(!execution)return DGGenerateAutomaticCubeMipmap(face,base,last);
    const struct DGMipmapExecution *saved=dg_mipmap_execution;GLIContext renderer=dg_mipmap_renderer;
    CGLContextObj context=CGLGetCurrentContext();
    dg_mipmap_execution=execution;dg_mipmap_renderer=context?context->rend:NULL;
    GLenum error=DGGenerateAutomaticCubeMipmap(face,base,last);
    dg_mipmap_execution=saved;dg_mipmap_renderer=renderer;return error;
}
static GLenum DGGenerateSrgbMipmapExecute(GLenum target,int legacy,int inside_begin,
    const struct DGMipmapExecution *execution)
{
    if(!execution)return DGGenerateSrgbMipmap(target,legacy,inside_begin);
    const struct DGMipmapExecution *saved=dg_mipmap_execution;GLIContext renderer=dg_mipmap_renderer;
    CGLContextObj context=CGLGetCurrentContext();
    dg_mipmap_execution=execution;dg_mipmap_renderer=context?context->rend:NULL;
    GLenum error=DGGenerateSrgbMipmap(target,legacy,inside_begin);
    dg_mipmap_execution=saved;dg_mipmap_renderer=renderer;return error;
}
#undef glBindTexture
#undef glTexParameteri
#undef glTexImage1D
#undef glTexImage2D
#undef glTexImage3D
#undef glTexSubImage1D
#undef glTexSubImage2D
#undef glTexSubImage3D
#undef glCopyTexSubImage1D
#undef glCopyTexSubImage2D
#undef glCopyTexSubImage3D
#undef glEnable
#undef glDisable
#undef glPixelTransferf
#undef glPixelTransferi
#undef glMatrixMode
#undef glLoadIdentity
#undef glLoadMatrixf
#undef glReadBuffer
#undef DG_MIPMAP_CALL
#endif
