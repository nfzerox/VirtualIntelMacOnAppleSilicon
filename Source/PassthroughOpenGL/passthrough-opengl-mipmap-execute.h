/* SDK-declared CGL callbacks, captured before an application starts a list.
 * Internal helper commands must execute, never enter the caller's list.
 * No native dispatch pointer is modified and no extra CGL context is needed. */
#ifndef VIM_MIPMAP_EXECUTE_H
#define VIM_MIPMAP_EXECUTE_H
#include <OpenGL/CGLContext.h>
#define DG_MIPMAP_EXECUTABLE(X) \
    X(bind_texture) X(tex_parameteri) \
    X(tex_image1D) X(tex_image2D) X(tex_image3D) \
    X(tex_sub_image1D) X(tex_sub_image2D) X(tex_sub_image3D) \
    X(copy_tex_sub_image1D) X(copy_tex_sub_image2D) X(copy_tex_sub_image3D) \
    X(enable) X(disable) X(pixel_transferf) X(pixel_transferi) \
    X(matrix_mode) X(load_identity) X(load_matrixf) X(read_buffer)
struct DGMipmapExecution {
#define DG_MIPMAP_FIELD(name) __typeof__(((GLIFunctionDispatch *)0)->name) name;
    DG_MIPMAP_EXECUTABLE(DG_MIPMAP_FIELD)
#undef DG_MIPMAP_FIELD
};
static void DGMipmapCaptureExecution(CGLContextObj context,struct DGMipmapExecution *out)
{
#define DG_MIPMAP_CAPTURE(name) out->name=context->disp.name;
    DG_MIPMAP_EXECUTABLE(DG_MIPMAP_CAPTURE)
#undef DG_MIPMAP_CAPTURE
}
#undef DG_MIPMAP_EXECUTABLE
static __thread const struct DGMipmapExecution *dg_mipmap_execution;
static __thread GLIContext dg_mipmap_renderer;
#endif

/* Lexical to the mipmap helper; all are undefined at its end. The API-name
 * fallback deliberately remains the ordinary libGL call (macro recursion
 * suppression), used by standalone controls without a captured executor. */
#define DG_MIPMAP_CALL(field,api,...) (dg_mipmap_execution ? dg_mipmap_execution->field(dg_mipmap_renderer,__VA_ARGS__) : api(__VA_ARGS__))
#define glBindTexture(...) DG_MIPMAP_CALL(bind_texture,glBindTexture,__VA_ARGS__)
#define glTexParameteri(...) DG_MIPMAP_CALL(tex_parameteri,glTexParameteri,__VA_ARGS__)
#define glTexImage1D(...) DG_MIPMAP_CALL(tex_image1D,glTexImage1D,__VA_ARGS__)
#define glTexImage2D(...) DG_MIPMAP_CALL(tex_image2D,glTexImage2D,__VA_ARGS__)
#define glTexImage3D(...) DG_MIPMAP_CALL(tex_image3D,glTexImage3D,__VA_ARGS__)
#define glTexSubImage1D(...) DG_MIPMAP_CALL(tex_sub_image1D,glTexSubImage1D,__VA_ARGS__)
#define glTexSubImage2D(...) DG_MIPMAP_CALL(tex_sub_image2D,glTexSubImage2D,__VA_ARGS__)
#define glTexSubImage3D(...) DG_MIPMAP_CALL(tex_sub_image3D,glTexSubImage3D,__VA_ARGS__)
#define glCopyTexSubImage1D(...) DG_MIPMAP_CALL(copy_tex_sub_image1D,glCopyTexSubImage1D,__VA_ARGS__)
#define glCopyTexSubImage2D(...) DG_MIPMAP_CALL(copy_tex_sub_image2D,glCopyTexSubImage2D,__VA_ARGS__)
#define glCopyTexSubImage3D(...) DG_MIPMAP_CALL(copy_tex_sub_image3D,glCopyTexSubImage3D,__VA_ARGS__)
#define glEnable(...) DG_MIPMAP_CALL(enable,glEnable,__VA_ARGS__)
#define glDisable(...) DG_MIPMAP_CALL(disable,glDisable,__VA_ARGS__)
#define glPixelTransferf(...) DG_MIPMAP_CALL(pixel_transferf,glPixelTransferf,__VA_ARGS__)
#define glPixelTransferi(...) DG_MIPMAP_CALL(pixel_transferi,glPixelTransferi,__VA_ARGS__)
#define glMatrixMode(...) DG_MIPMAP_CALL(matrix_mode,glMatrixMode,__VA_ARGS__)
#define glLoadIdentity() (dg_mipmap_execution ? dg_mipmap_execution->load_identity(dg_mipmap_renderer) : glLoadIdentity())
#define glLoadMatrixf(...) DG_MIPMAP_CALL(load_matrixf,glLoadMatrixf,__VA_ARGS__)
#define glReadBuffer(...) DG_MIPMAP_CALL(read_buffer,glReadBuffer,__VA_ARGS__)
