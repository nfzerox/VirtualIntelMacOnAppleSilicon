/* Context-free retained-surface service ABI. No GL SDK dispatch layout,
 * user virtual addresses or host pointers are part of this handshake.
 * The namespace identifies a guest boot, not an authentication boundary. */
#ifndef VIM_GL_SURFACE_SERVICE_H
#define VIM_GL_SURFACE_SERVICE_H
#include <stdint.h>
#define VIM_GL_BOOT_NAMESPACE_PROPERTY "VIMPassthroughOpenGLBootNamespace"
#define VIM_GL_SERVICE_HELLO 0x10033u
#define VIM_GL_SERVICE_MAGIC 0x56534731u
#define VIM_GL_SERVICE_VERSION 1u
#define VIM_GL_SERVICE_HOLD 0x10013u
#define VIM_GL_SERVICE_UNHOLD 0x10014u
#define VIM_GL_SERVICE_READ 0x10032u
#define VIM_GL_SERVICE_PRESENT 0x1003fu
#define VIM_GL_SERVICE_READY 0x10040u
/* Explicit asynchronous-backing experiment only. A completion ticket carries
 * no pixels or process pointer; Wait acknowledges actual native completion.
 * Release is valid only after a successful Wait, never as cancellation. */
#define VIM_GL_SERVICE_BACKING_WAIT 0x10050u
#define VIM_GL_SERVICE_BACKING_RELEASE 0x10051u
#define VIM_GL_SERVICE_BACKING_CHECK 0x10052u
#define VIM_GL_SERVICE_CAP_BACKING_TICKETS (UINT64_C(1)<<48)
#define VIM_GL_SERVICE_REPLY 1u
#define VIM_GL_SERVICE_OUTPUT_ONLY 2u
#define VIM_GL_SERVICE_CAP_HOLDS 4u
#define VIM_GL_SERVICE_CAP_READ 131072u
#define VIM_GL_SERVICE_CAP_VERSIONS 524288u
#define VIM_GL_SERVICE_CAP_PRESENT 536870912u
struct VIMGLServiceMessage { uint32_t op,context,bytes,flags;uint64_t args[16]; };
struct VIMGLServiceReply { uint32_t bytes,error;uint64_t result; };
/* Bounded research qualification only: read a 2x2 region of an owned test
 * resource through the provider service. No user address or arbitrary opcode. */
struct VIMGLServiceProbe { uint64_t namespace_id[2],object,incarnation,epoch;uint32_t width,height,x,y; };
/* Materialize into the caller's locked real surface, not a user pointer.
 * CPU epoch is a compare-before-write guard; host epoch identifies pixels. */
struct VIMSurfaceMaterialize { uint64_t namespace_id[2],object,incarnation,cpu_epoch,gpu_epoch; };
typedef char VIMSurfaceMaterializeSize[(sizeof(struct VIMSurfaceMaterialize)==48)?1:-1];
typedef char VIMGLServiceProbeSize[(sizeof(struct VIMGLServiceProbe)==56)?1:-1];
typedef char VIMGLServiceMessageSize[(sizeof(struct VIMGLServiceMessage)==144)?1:-1];
typedef char VIMGLServiceReplySize[(sizeof(struct VIMGLServiceReply)==16)?1:-1];
#endif
