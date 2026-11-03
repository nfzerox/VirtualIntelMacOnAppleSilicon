/* Local/private PassthroughOpenGL experiment, not an upstream contribution.
 * Native callbacks touch only their retained job and schedule a main-loop BH.
 * Device/VRAM/IRQ state is accessed only under the BQL. Reset never cancels a
 * native wait or mistakes cancellation for completed GPU use: pending records
 * remain bounded/retained until their actual callback, then discard old epochs.
 * The guest pin owner must independently survive until verified completion.
 */
#include "qemu/atomic.h"
struct VIMPassthroughOpenGLNotify {
    struct vmsvga_state_s *device;
    QEMUBH *bh;
    uint64_t epoch, cookie, ticket, namespace_id[2];
    unsigned slot;
    int status;
    bool done;
};
static void vmsvga_legacy_irq_update(struct vmsvga_state_s *s)
{
    if (s->legacy_panel_pci && qdev_is_realized(DEVICE(s->legacy_panel_pci))) {
        bool panel = s->legacy_panel_irq_enabled &&
                     s->legacy_panel_generation != s->legacy_panel_ack;
        bool backing = s->passthrough_opengl_notification_irq_enabled &&
                       s->passthrough_opengl_notifications_done;
        pci_set_irq(s->legacy_panel_pci, s->passthrough_opengl_bridge && (panel || backing));
    }
}
static void vmsvga_passthrough_opengl_notify_free(struct VIMPassthroughOpenGLNotify *job)
{
    PCIDevice *pci = job->device->legacy_panel_pci;
    if (job->bh) {
        qemu_bh_delete(job->bh);
    }
    g_free(job);
    object_unref(OBJECT(pci));
}
static void vmsvga_passthrough_opengl_notify_bh(void *opaque)
{
    struct VIMPassthroughOpenGLNotify *job = opaque;
    struct vmsvga_state_s *s = job->device;
    assert(bql_locked());
    assert(s->passthrough_opengl_notifications[job->slot] == job);
    qemu_bh_delete(job->bh);
    job->bh = NULL;
    if (job->epoch != s->passthrough_opengl_epoch ||
        !qdev_is_realized(DEVICE(s->legacy_panel_pci))) {
        if (!qatomic_read(&job->status)) {
            vmsvga_gl_backing_release_completed(job->namespace_id, job->ticket);
        }
        s->passthrough_opengl_notifications[job->slot] = NULL;
        vmsvga_passthrough_opengl_notify_free(job);
        return;
    }
    job->done = true;
    s->passthrough_opengl_notifications_done++;
    vmsvga_legacy_irq_update(s);
}
static void vmsvga_passthrough_opengl_notify_native(void *opaque, int status)
{
    struct VIMPassthroughOpenGLNotify *job = opaque;
    /* This may run BEFORE subscribe returns. All job/BH/QOM state has
     * already been published. The scheduler is thread-safe; no BQL here. */
    qatomic_set(&job->status, status);
    qemu_bh_schedule(job->bh);
}
static int vmsvga_passthrough_opengl_notify_subscribe(struct vmsvga_state_s *s, const uint8_t *request)
{
    uint64_t namespace_id[2] = { ldq_le_p(request), ldq_le_p(request + 8) };
    uint64_t ticket = ldq_le_p(request + 16), cookie = ldq_le_p(request + 24);
    unsigned slot;
    if (!cookie || !ticket || !(namespace_id[0] | namespace_id[1]) ||
        !vmsvga_gl_backing_notify_available()) {
        return EINVAL;
    }
    for (slot = 0; slot < ARRAY_SIZE(s->passthrough_opengl_notifications); slot++) {
        struct VIMPassthroughOpenGLNotify *job = s->passthrough_opengl_notifications[slot];
        if (job && job->epoch == s->passthrough_opengl_epoch && job->cookie == cookie) {
            return EALREADY;
        }
    }
    for (slot = 0; slot < ARRAY_SIZE(s->passthrough_opengl_notifications); slot++) {
        if (!s->passthrough_opengl_notifications[slot]) {
            break;
        }
    }
    if (slot == ARRAY_SIZE(s->passthrough_opengl_notifications)) {
        return EAGAIN;
    }
    struct VIMPassthroughOpenGLNotify *job = g_new0(struct VIMPassthroughOpenGLNotify, 1);
    job->device = s;
    job->epoch = s->passthrough_opengl_epoch;
    job->ticket = ticket;
    job->cookie = cookie;
    job->slot = slot;
    memcpy(job->namespace_id, namespace_id, sizeof(namespace_id));
    object_ref(OBJECT(s->legacy_panel_pci));
    job->bh = qemu_bh_new(vmsvga_passthrough_opengl_notify_bh, job);
    s->passthrough_opengl_notifications[slot] = job;
    int error = vmsvga_gl_backing_subscribe(namespace_id, ticket,
                                         vmsvga_passthrough_opengl_notify_native, job);
    if (error) {
        /* The private API guarantees NO callback on rejection. */
        s->passthrough_opengl_notifications[slot] = NULL;
        vmsvga_passthrough_opengl_notify_free(job);
    }
    return error;
}
static int32_t vmsvga_passthrough_opengl_notify_pop(struct vmsvga_state_s *s, uint8_t *result)
{
    for (unsigned slot = 0; slot < ARRAY_SIZE(s->passthrough_opengl_notifications); slot++) {
        struct VIMPassthroughOpenGLNotify *job = s->passthrough_opengl_notifications[slot];
        if (!job || !job->done) {
            continue;
        }
        assert(job->epoch == s->passthrough_opengl_epoch);
        stq_le_p(result, job->cookie);
        stq_le_p(result + 8, job->ticket);
        stl_le_p(result + 16, qatomic_read(&job->status));
        stl_le_p(result + 20, 0);
        s->passthrough_opengl_notifications[slot] = NULL;
        s->passthrough_opengl_notifications_done--;
        vmsvga_legacy_irq_update(s);
        vmsvga_passthrough_opengl_notify_free(job);
        return 24;
    }
    return 0;
}
static void vmsvga_passthrough_opengl_notify_reset(struct vmsvga_state_s *s)
{
    s->passthrough_opengl_notification_irq_enabled = false;
    s->passthrough_opengl_notifications_done = 0;
    for (unsigned slot = 0; slot < ARRAY_SIZE(s->passthrough_opengl_notifications); slot++) {
        struct VIMPassthroughOpenGLNotify *job = s->passthrough_opengl_notifications[slot];
        if (job && job->done) {
            if (!qatomic_read(&job->status)) {
                vmsvga_gl_backing_release_completed(job->namespace_id, job->ticket);
            }
            s->passthrough_opengl_notifications[slot] = NULL;
            vmsvga_passthrough_opengl_notify_free(job);
        }
        /* Pending old jobs keep their slots/QOM references until real
         * completion. Their BH drops them without writing new guest VRAM. */
    }
}
