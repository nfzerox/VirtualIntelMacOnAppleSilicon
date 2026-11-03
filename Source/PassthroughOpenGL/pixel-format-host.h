/* Native storage parameters for canonical typed private surfaces. */
#ifndef VIM_GL_PIXEL_FORMAT_HOST_H
#define VIM_GL_PIXEL_FORMAT_HOST_H
#include "../vendor/source/VMsvga2/vminclude/vim_gl_pixel_formats.h"
static GLenum DGSurfaceGLInternal(unsigned format)
{return format==VIM_GL_PIXEL_RGBA16?GL_RGBA16:GL_RGBA8;}
static GLenum DGSurfaceGLFormat(unsigned format)
{return format==VIM_GL_PIXEL_RGBA16?GL_RGBA:GL_BGRA;}
static GLenum DGSurfaceGLType(unsigned format)
{return format==VIM_GL_PIXEL_RGBA16?GL_UNSIGNED_SHORT:GL_UNSIGNED_INT_8_8_8_8_REV;}
static uint32_t DGSurfaceNativeFormat(unsigned format)
{return format==VIM_GL_PIXEL_RGBA16?0x6c363472U:0x42475241U;}
static int DGSurfaceInternalMatches(unsigned format,GLenum internal)
{
    return format==VIM_GL_PIXEL_RGBA16?(internal==GL_RGBA16 || internal==GL_RGBA || internal==GL_RGB):
        (internal==GL_RGB || internal==GL_RGB8 || internal==GL_RGBA || internal==GL_RGBA8);
}
#endif
