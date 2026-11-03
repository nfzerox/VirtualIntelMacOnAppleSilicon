/* PRIVATE kernel-to-kernel research interface. Never accept this structure
 * from user space, serialize it to the device or expose a host pointer.
 * The receiver retains owner through ONE callback after native completion.
 * Failed notification grants NO CPU ownership. Registration is not completion.
 */
#ifndef VIM_BACKING_NOTIFY_KERNEL_H
#define VIM_BACKING_NOTIFY_KERNEL_H
#ifdef KERNEL
#define VIM_BACKING_NOTIFY_AVAILABLE "VIMDirectGLBackingNotifyAvailable-v1"
#define VIM_BACKING_NOTIFY_SUBSCRIBE "VIMDirectGLBackingNotifySubscribe-v1"
struct VIMBackingKernelSubscription {
    uint32_t version,bytes;
    uint64_t namespace_id[2],ticket;
    OSObject *owner;
    void (*completed)(OSObject *,uint64_t,IOReturn);
};
#endif
#endif
