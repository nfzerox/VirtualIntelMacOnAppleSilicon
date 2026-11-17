/* Headless PassthroughOpenGL command endpoint. No framebuffer, display modes,
 * guest virtual addresses or native pointers in this PCI ABI. Both guest ABIs
 * use the existing VIMGLDevicePacket and canonical GL interpreter messages. */
#ifndef VIM_GL_HEADLESS_H
#define VIM_GL_HEADLESS_H
#include "vim_passthrough_opengl.h"
#define VIM_GL_HEADLESS_VENDOR 0x1b36u
#define VIM_GL_HEADLESS_DEVICE 0x10f0u
#define VIM_GL_HEADLESS_VERSION 1u
#define VIM_GL_HEADLESS_SLOTS 64u
#define VIM_GL_HEADLESS_SLOT_BYTES (512u*1024u)
#define VIM_GL_HEADLESS_MAILBOX_BYTES (VIM_GL_HEADLESS_SLOTS*VIM_GL_HEADLESS_SLOT_BYTES)
#define VIM_GL_HEADLESS_MAGIC_REGISTER 0u
#define VIM_GL_HEADLESS_VERSION_REGISTER 4u
#define VIM_GL_HEADLESS_DOORBELL 8u
/* Append-only feature query. OPEN requests the intersection implemented by
 * both the driver and device; an older driver must not acquire new methods. */
#define VIM_GL_HEADLESS_CAPABILITIES_REGISTER 12u
#define VIM_GL_HEADLESS_RAM_CAPABILITIES (VIM_GL_RESULT_CAP_SCATTER | \
    VIM_GL_RESULT_CAP_RAM_SCATTER | VIM_GL_RESULT_CAP_ROW_UPLOAD)
#define VIM_GL_HEADLESS_CLOSE 4u
#endif
