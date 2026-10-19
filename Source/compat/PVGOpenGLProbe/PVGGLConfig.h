#ifndef PVG_GL_CONFIG_H
#define PVG_GL_CONFIG_H
#include <stddef.h>

/* Observed x86_64 GLD ABI: Mojave ends at 0x250; the donor appends
 * primitive policy and a limit through 0x260. This adapter must be selected
 * by a Mojave-only GLD entry layer, not applied to Catalina or i386. */
enum { PVGGLNativeConfigSize = 0x250, PVGGLDonorConfigSize = 0x260 };
typedef struct PVGGLConfig PVGGLConfig;

/* The native storage is borrowed and must outlive this adapter. The caller
 * serializes access with the GL context; no global table or lock is needed. */
PVGGLConfig *PVGGLConfigCreate(void *native, size_t nativeSize);
void *PVGGLConfigDonor(PVGGLConfig *config);

/* Publish the common prefix after donor population/group-mode updates.
 * The donor extension NEVER goes back into native storage. Keeps the native
 * mode-0 frontend contract proven by the isolated bring-up probes. */
int PVGGLConfigPublish(PVGGLConfig *config);

/* Call only after the donor context has finished using the config. */
void PVGGLConfigDestroy(PVGGLConfig *config);
#endif
