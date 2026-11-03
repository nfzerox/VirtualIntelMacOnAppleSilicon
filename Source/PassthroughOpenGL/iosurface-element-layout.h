/* Native single-plane IOSurface formats carried by the import protocol.
 * No pixel conversion: host CGL retains the requested sampling semantics. */
#ifndef VIM_IOSURFACE_ELEMENT_LAYOUT_H
#define VIM_IOSURFACE_ELEMENT_LAYOUT_H
#include <OpenGL/gl.h>
static unsigned DGIOSurfaceElementBytes(unsigned internal)
{
    switch(internal){
    case GL_RGB:case GL_RGB8:case GL_RGBA:case GL_RGBA8:return 4;
    case GL_LUMINANCE8:case 0x8229:return 1; /* GL_R8, not luminance sampling */
    case GL_LUMINANCE8_ALPHA8:return 2;
    case 0x881a:return 8; /* GL_RGBA16F_ARB, native RGhA */
    case 0x8814:return 16; /* GL_RGBA32F_ARB, native RGfA */
    default:return 0;
    }
}
static unsigned DGIOSurfacePixelFormat(unsigned bytes)
{
    switch(bytes){
    case 1:return 0x4c303038;case 2:return 0x32433038;
    case 8:return 0x52476841;case 16:return 0x52476641;
    default:return 0x42475241;
    }
}
static unsigned DGIOSurfaceGLFormat(unsigned bytes)
{ return bytes==1?GL_LUMINANCE:bytes==2?GL_LUMINANCE_ALPHA:bytes>4?GL_RGBA:GL_BGRA; }
static unsigned DGIOSurfaceInternalGLFormat(unsigned internal)
{ return internal==0x8229?0x1903:DGIOSurfaceGLFormat(DGIOSurfaceElementBytes(internal)); } /* GL_RED */
static unsigned DGIOSurfaceGLType(unsigned bytes)
{ return bytes<4?GL_UNSIGNED_BYTE:bytes==8?0x140b:bytes==16?GL_FLOAT:GL_UNSIGNED_INT_8_8_8_8_REV; }
/* Snow's CGLIOSurface.h explicitly permits these native video bindings.
 * RGB_422 exposes raw Cr/Y/Cb channels; YCBCR_422 performs native conversion.
 * The same RGB internal format can therefore describe BGRA4 or packed4222.
 */
static int DGIOSurfacePacked422(unsigned internal,unsigned format,unsigned type)
{
    return (internal==GL_RGB||internal==GL_RGB8)&&
        (format==0x85b9||format==0x8a1f)&&(type==0x85ba||type==0x85bb);
}
static unsigned DGIOSurfaceTransferBytes(unsigned internal,unsigned format,unsigned type)
{
    if(DGIOSurfacePacked422(internal,format,type))return 2;
    unsigned bytes=DGIOSurfaceElementBytes(internal);
    return bytes&&format==DGIOSurfaceInternalGLFormat(internal)&&type==DGIOSurfaceGLType(bytes)?bytes:0;
}
static unsigned DGIOSurfaceTransferPixelFormat(unsigned internal,unsigned format,unsigned type)
{
    /* Both protocol hosts and guests are little-endian. Native explicit
     * format/type retain raw-vs-converted sampling independently of FourCC. */
    return DGIOSurfacePacked422(internal,format,type)?
        (type==0x85ba?0x32767579:0x79757673):DGIOSurfacePixelFormat(DGIOSurfaceElementBytes(internal));
}
#endif
