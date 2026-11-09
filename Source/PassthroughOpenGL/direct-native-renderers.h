/* Additive native-CGL renderer controls shared by the wire and guest GLI. */
#ifndef VIM_DIRECT_NATIVE_RENDERERS_H
#define VIM_DIRECT_NATIVE_RENDERERS_H
#include <stdint.h>
#define DG_NATIVE_HARDWARE 1u
#define DG_NATIVE_SOFTWARE 2u
#define DG_NATIVE_CURRENT_SOFTWARE 4u
/* The numeric SDK selector also compiles against period pre-4.1 headers. */
#define DG_CGL_PROFILE_GL4 0x4100u
struct DGNativeRendererInfo {uint32_t flags,renderer;};
_Static_assert(sizeof(struct DGNativeRendererInfo)==8,"fixed native renderer metadata");
extern int DGQueryNativeRenderer(struct DGNativeRendererInfo *);
extern int DGSelectNativeRenderer(unsigned,struct DGNativeRendererInfo *);
extern int DGQueryRendererDetails(int32_t *,int32_t *,struct DGNativeRendererInfo *);
extern int DGQueryRendererDetailsWithGL4(int32_t *,int32_t *,struct DGNativeRendererInfo *,int *);
#endif
