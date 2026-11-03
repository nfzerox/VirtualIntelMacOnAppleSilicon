/* Local host-only IOSurface capability transport. No global IOSurface IDs,
 * pixel payloads or guest pointers. Endpoint discovery belongs to the app.
 * Caller owns timeout/cancellation and must authenticate its rendezvous.
 */
#ifndef VIM_DISPLAY_SURFACE_MESSAGE_H
#define VIM_DISPLAY_SURFACE_MESSAGE_H
#include <IOSurface/IOSurface.h>
#include <mach/mach.h>
#include <bsm/libbsm.h>
#include <stdint.h>
#include <string.h>
#define VIM_SURFACE_FRAME_MESSAGE 0x56494d53
struct VIMSurfaceFrameMessage {
    mach_msg_header_t header;
    mach_msg_body_t body;
    mach_msg_port_descriptor_t surface;
    uint64_t generation,ticket;
    uint32_t slot,width,height,version;
};
enum VIMSurfaceReleaseStatus { VIMSurfaceSkipped=0, VIMSurfaceGPUCompleted=1,
    VIMSurfaceFailed=2 };
struct VIMSurfaceReleaseMessage {
    mach_msg_header_t header;
    uint64_t generation,ticket;
    uint32_t slot,status;
};
/* Consumes the one-use reply right on every path. Call only after the consumer
 * has finished touching source storage (including skipped/failed frames). */
static inline kern_return_t VIMSurfaceFrameRelease(struct VIMSurfaceFrameMessage *frame,
    uint32_t status,mach_msg_timeout_t timeout)
{
    struct VIMSurfaceReleaseMessage m;memset(&m,0,sizeof(m));
    mach_port_t reply=frame->header.msgh_remote_port;
    if(!reply)return KERN_INVALID_ARGUMENT;
    frame->header.msgh_remote_port=MACH_PORT_NULL;
    if(status>VIMSurfaceFailed){mach_port_deallocate(mach_task_self(),reply);return KERN_INVALID_ARGUMENT;}
    m.header.msgh_bits=MACH_MSGH_BITS(MACH_MSG_TYPE_MOVE_SEND_ONCE,0);
    m.header.msgh_remote_port=reply;m.header.msgh_size=sizeof(m);
    m.header.msgh_id=VIM_SURFACE_FRAME_MESSAGE+1;
    m.generation=frame->generation;m.ticket=frame->ticket;m.slot=frame->slot;m.status=status;
    kern_return_t kr=mach_msg(&m.header,MACH_SEND_MSG|MACH_SEND_TIMEOUT,sizeof(m),0,0,timeout,0);
    if(kr!=KERN_SUCCESS)mach_port_deallocate(mach_task_self(),reply);
    return kr;
}
static inline kern_return_t VIMSurfaceReleaseReceive(mach_port_t port,
    uint64_t generation,uint64_t ticket,uint32_t slot,uint32_t *status,mach_msg_timeout_t timeout)
{
    struct {struct VIMSurfaceReleaseMessage m;mach_msg_max_trailer_t trailer;} p;
    memset(&p,0,sizeof(p));
    kern_return_t kr=mach_msg(&p.m.header,MACH_RCV_MSG|MACH_RCV_TIMEOUT,0,sizeof(p),port,timeout,0);
    if(kr)return kr;
    if(p.m.header.msgh_size!=sizeof(p.m) || p.m.header.msgh_id!=VIM_SURFACE_FRAME_MESSAGE+1 ||
       (p.m.header.msgh_bits&MACH_MSGH_BITS_COMPLEX) || p.m.generation!=generation ||
       p.m.ticket!=ticket || p.m.slot!=slot || p.m.status>VIMSurfaceFailed){
        mach_msg_destroy(&p.m.header);return KERN_INVALID_ARGUMENT;
    }
    *status=p.m.status;mach_msg_destroy(&p.m.header);return KERN_SUCCESS;
}
static inline kern_return_t VIMSurfaceFrameSend(mach_port_t destination,
    mach_port_t reply,IOSurfaceRef surface,uint64_t generation,uint64_t ticket,
    uint32_t slot,uint32_t width,uint32_t height,mach_msg_timeout_t timeout)
{
    struct VIMSurfaceFrameMessage m;memset(&m,0,sizeof(m));
    mach_port_t capability=IOSurfaceCreateMachPort(surface);
    if(!capability)return KERN_INVALID_ARGUMENT;
    m.header.msgh_bits=MACH_MSGH_BITS_COMPLEX|
        MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,MACH_MSG_TYPE_MAKE_SEND_ONCE);
    m.header.msgh_size=sizeof(m);m.header.msgh_remote_port=destination;
    m.header.msgh_local_port=reply;m.header.msgh_id=VIM_SURFACE_FRAME_MESSAGE;
    m.body.msgh_descriptor_count=1;m.surface.name=capability;
    m.surface.disposition=MACH_MSG_TYPE_COPY_SEND;m.surface.type=MACH_MSG_PORT_DESCRIPTOR;
    m.generation=generation;m.ticket=ticket;m.slot=slot;m.width=width;m.height=height;m.version=1;
    kern_return_t kr=mach_msg(&m.header,MACH_SEND_MSG|MACH_SEND_TIMEOUT,sizeof(m),0,MACH_PORT_NULL,timeout,MACH_PORT_NULL);
    mach_port_deallocate(mach_task_self(),capability);return kr;
}
/* Successful receive transfers a send-once reply right and retained surface
 * to caller. All malformed/wrong-user messages have their rights destroyed. */
static inline kern_return_t VIMSurfaceFrameReceive(mach_port_t port,uid_t user,pid_t renderer,
    struct VIMSurfaceFrameMessage *out,IOSurfaceRef *surface,mach_msg_timeout_t timeout)
{
    struct {struct VIMSurfaceFrameMessage message;mach_msg_max_trailer_t trailer;} packet;
    memset(&packet,0,sizeof(packet));*surface=NULL;
    kern_return_t kr=mach_msg(&packet.message.header,
        MACH_RCV_MSG|MACH_RCV_TIMEOUT|MACH_RCV_TRAILER_TYPE(MACH_MSG_TRAILER_FORMAT_0)|
        MACH_RCV_TRAILER_ELEMENTS(MACH_RCV_TRAILER_AUDIT),0,sizeof(packet),port,timeout,MACH_PORT_NULL);
    if(kr!=KERN_SUCCESS)return kr;
    struct VIMSurfaceFrameMessage *m=&packet.message;
    int valid=m->header.msgh_size==sizeof(*m) && m->header.msgh_id==VIM_SURFACE_FRAME_MESSAGE &&
        (m->header.msgh_bits&MACH_MSGH_BITS_COMPLEX) && m->body.msgh_descriptor_count==1 &&
        m->surface.type==MACH_MSG_PORT_DESCRIPTOR && m->surface.disposition==MACH_MSG_TYPE_PORT_SEND &&
        MACH_MSGH_BITS_REMOTE(m->header.msgh_bits)==MACH_MSG_TYPE_PORT_SEND_ONCE &&
        m->version==1 && m->generation && m->ticket && m->slot<3 &&
        m->width>=320 && m->height>=200 && m->width<=8192 && m->height<=8192;
    if(valid){
        mach_msg_audit_trailer_t *t=(void *)((char *)&packet+round_msg(m->header.msgh_size));
        valid=t->msgh_trailer_type==MACH_MSG_TRAILER_FORMAT_0 &&
            t->msgh_trailer_size>=sizeof(*t) && audit_token_to_euid(t->msgh_audit)==user &&
            renderer>0 && audit_token_to_pid(t->msgh_audit)==renderer;
    }
    IOSurfaceRef imported=valid?IOSurfaceLookupFromMachPort(m->surface.name):NULL;
    if(!imported){mach_msg_destroy(&m->header);return KERN_INVALID_ARGUMENT;}
    *out=*m;*surface=imported;
    mach_port_deallocate(mach_task_self(),m->surface.name);
    out->surface.name=MACH_PORT_NULL;return KERN_SUCCESS;
}
#endif
