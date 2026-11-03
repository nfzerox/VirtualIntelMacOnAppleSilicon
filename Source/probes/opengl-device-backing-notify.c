/* Local experiment: exercise the ACTUAL device completion implementation
 * with a deterministic BH/QOM/IRQ/native-callback harness. This is not a GPU,
 * real PCI-interrupt or desktop performance qualification. Actual GPU callback
 * and first-pixel checks live in opengl-direct-backing-scope-wire.c. */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "hw/display/vim-passthrough-opengl-backing-notify.h"
#undef g_new0
#undef g_free
#define g_new0(type,count) ((type *)calloc((count),sizeof(type)))
#define g_free free
#define OBJECT(p) (p)
#define DEVICE(p) (p)
struct PCIDevice { unsigned refs;bool realized;int irq; };
typedef struct QEMUBH { void (*callback)(void *);void *opaque;bool scheduled; } QEMUBH;
static QEMUBH *bottom_halves[256];
static bool held_bql=true,available=true,early_callback;
static unsigned native_count;
static unsigned retired_count;
static int native_error;
static struct { VIMGLBackingNotifyCallback callback;void *opaque; } native_jobs[256];
struct vmsvga_state_s {
    PCIDevice *legacy_panel_pci;
    bool passthrough_opengl_bridge,legacy_panel_irq_enabled,passthrough_opengl_notification_irq_enabled;
    unsigned passthrough_opengl_notifications_done;
    uint32_t legacy_panel_generation,legacy_panel_ack;
    uint64_t passthrough_opengl_epoch;
    struct VIMPassthroughOpenGLNotify *passthrough_opengl_notifications[256];
};
static bool bql_locked(void) { return held_bql; }
static bool qdev_is_realized(PCIDevice *p) { assert(held_bql);return p->realized; }
static void pci_set_irq(PCIDevice *p,int value) { assert(held_bql);p->irq=value; }
static void object_ref(PCIDevice *p) { assert(held_bql);++p->refs; }
static void object_unref(PCIDevice *p) { assert(held_bql && p->refs>1);--p->refs; }
static QEMUBH *qemu_bh_new(void (*callback)(void *),void *opaque)
{
    assert(held_bql);
    for(unsigned i=0;i<256;++i)if(!bottom_halves[i]){
        QEMUBH *bh=calloc(1,sizeof(*bh));assert(bh);bh->callback=callback;bh->opaque=opaque;
        return bottom_halves[i]=bh;
    }
    abort();
}
static void qemu_bh_delete(QEMUBH *bh)
{
    assert(held_bql);
    for(unsigned i=0;i<256;++i)if(bottom_halves[i]==bh){bottom_halves[i]=NULL;free(bh);return;}
    abort();
}
static void qemu_bh_schedule(QEMUBH *bh) { assert(!held_bql);bh->scheduled=true; }
static void pump(void)
{
    assert(held_bql);
    for(unsigned i=0;i<256;++i){QEMUBH *bh=bottom_halves[i];if(bh && bh->scheduled){bh->scheduled=false;bh->callback(bh->opaque);}}
}
static bool vmsvga_gl_backing_notify_available(void) { return available; }
static int vmsvga_gl_backing_release_completed(const uint64_t space[2],uint64_t ticket)
{ assert(held_bql && space[0]==11 && space[1]==22 && ticket==33);++retired_count;return 0; }
static int vmsvga_gl_backing_subscribe(const uint64_t space[2],uint64_t ticket,VIMGLBackingNotifyCallback callback,void *opaque)
{
    assert(held_bql && (space[0]|space[1]) && ticket);
    if(native_error)return native_error;
    if(early_callback){held_bql=false;callback(opaque,0);held_bql=true;}
    else {assert(native_count<256);native_jobs[native_count].callback=callback;native_jobs[native_count++].opaque=opaque;}
    return 0;
}
#include "../vendor/qemu/hw/display/vmware_passthrough_opengl_notify.inc"
static void finish(unsigned i,int status)
{
    assert(i<native_count && native_jobs[i].callback);held_bql=false;
    native_jobs[i].callback(native_jobs[i].opaque,status);native_jobs[i].callback=NULL;
    held_bql=true;pump();
}
static void reset(struct vmsvga_state_s *s)
{
    ++s->passthrough_opengl_epoch;vmsvga_passthrough_opengl_notify_reset(s);
    s->legacy_panel_generation=s->legacy_panel_ack=0;s->legacy_panel_irq_enabled=false;
    vmsvga_legacy_irq_update(s);
}
int main(void)
{
    PCIDevice pci={.refs=1,.realized=true};
    struct vmsvga_state_s s={.legacy_panel_pci=&pci,.passthrough_opengl_bridge=true,.passthrough_opengl_notification_irq_enabled=true};
    uint64_t request[4]={11,22,33,44};uint8_t result[24];
    assert(!vmsvga_passthrough_opengl_notify_subscribe(&s,(void *)request));
    assert(pci.refs==2 && !pci.irq && !s.passthrough_opengl_notifications_done);
    assert(vmsvga_passthrough_opengl_notify_subscribe(&s,(void *)request)==EALREADY);
    finish(0,0);assert(pci.irq && s.passthrough_opengl_notifications_done==1);
    s.legacy_panel_irq_enabled=true;s.legacy_panel_generation=1;
    assert(vmsvga_passthrough_opengl_notify_pop(&s,result)==24);
    assert(ldq_le_p(result)==44 && ldq_le_p(result+8)==33 && !memcmp(result+16,(uint8_t[8]){0},8));
    assert(pci.refs==1 && pci.irq);s.legacy_panel_ack=1;vmsvga_legacy_irq_update(&s);assert(!pci.irq);
    assert(!vmsvga_passthrough_opengl_notify_pop(&s,result));
    puts("PASS completion queue: bounded nonblocking admission, BH-only publication, exact result, independent resize IRQ");

    native_count=0;assert(!vmsvga_passthrough_opengl_notify_subscribe(&s,(void *)request));
    reset(&s);assert(pci.refs==2 && !pci.irq);memset(result,0xa7,sizeof(result));
    finish(0,0);assert(pci.refs==1 && !pci.irq && !s.passthrough_opengl_notifications_done && retired_count==1);
    assert(!vmsvga_passthrough_opengl_notify_pop(&s,result));
    for(unsigned i=0;i<24;++i)assert(result[i]==0xa7);
    puts("PASS pending reset: retains callback owner, rejects old-epoch delivery, no new guest-buffer writes");

    native_count=0;assert(!vmsvga_passthrough_opengl_notify_subscribe(&s,(void *)request));finish(0,0);
    assert(pci.refs==2 && s.passthrough_opengl_notifications_done==1);reset(&s);assert(pci.refs==1 && !s.passthrough_opengl_notifications_done && retired_count==2);
    native_error=EINVAL;assert(vmsvga_passthrough_opengl_notify_subscribe(&s,(void *)request)==EINVAL);assert(pci.refs==1);native_error=0;
    available=false;assert(vmsvga_passthrough_opengl_notify_subscribe(&s,(void *)request)==EINVAL);available=true;
    request[3]=0;assert(vmsvga_passthrough_opengl_notify_subscribe(&s,(void *)request)==EINVAL);
    puts("PASS completed reset and rejected admission: balanced references and no callback ownership leak");

    native_count=0;
    for(unsigned i=0;i<256;++i){request[3]=i+1;assert(!vmsvga_passthrough_opengl_notify_subscribe(&s,(void *)request));}
    request[3]=257;assert(vmsvga_passthrough_opengl_notify_subscribe(&s,(void *)request)==EAGAIN);reset(&s);
    assert(vmsvga_passthrough_opengl_notify_subscribe(&s,(void *)request)==EAGAIN && pci.refs==257);
    for(unsigned i=0;i<256;++i)finish(i,0);
    assert(pci.refs==1 && !s.passthrough_opengl_notifications_done && retired_count==258);
    puts("PASS 256 retained jobs: reset cannot bypass admission while native uses are still pending");

    native_count=0;assert(!vmsvga_passthrough_opengl_notify_subscribe(&s,(void *)request));finish(0,EIO);
    assert(vmsvga_passthrough_opengl_notify_pop(&s,result)==24);
    uint32_t status;memcpy(&status,result+16,4);assert(le32_to_cpu(status)==EIO && pci.refs==1 && retired_count==258);
    early_callback=true;assert(!vmsvga_passthrough_opengl_notify_subscribe(&s,(void *)request));
    assert(!s.passthrough_opengl_notifications_done);pump();assert(vmsvga_passthrough_opengl_notify_pop(&s,result)==24 && pci.refs==1);
    assert(!vmsvga_passthrough_opengl_notify_subscribe(&s,(void *)request));
    reset(&s);assert(pci.refs==2);pump();assert(pci.refs==1 && retired_count==259 && !s.passthrough_opengl_notifications_done);
    early_callback=false;native_count=0;assert(!vmsvga_passthrough_opengl_notify_subscribe(&s,(void *)request));
    pci.realized=false;finish(0,0);assert(pci.refs==1 && !s.passthrough_opengl_notifications_done);
    puts("PASS native failure, callback-before-return and unrealized-device completion: no false success or stale IRQ");
    return 0;
}
