/* Native, unprivileged ownership test of the ACTUAL GLI pool implementation.
 * No IOKit services/windows/VMs are opened. The real guest owns integration
 * coverage; this checks bounds, rejection, single ownership and teardown.
 */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
typedef uint32_t io_connect_t;
typedef int IOReturn;
enum {kIOVM2DSetSurface=0};
struct VIMContext {io_connect_t idle_drawable_connections[2];unsigned idle_drawable_connection_count;};
struct VIMCGSDrawable {io_connect_t updates;unsigned accelerator_surface,locked,backing,framebuffer,vram;};
static unsigned resets,destroys,closes;static int reject;
static IOReturn IOConnectCallMethod(io_connect_t port,unsigned selector,const uint64_t *args,
    unsigned count,const void *input,size_t bytes,uint64_t *out,unsigned *nout,void *record,size_t *size)
{
    assert(port && selector==kIOVM2DSetSurface && count==2 && !args[0] && !args[1]);
    assert(!input && !bytes && !out && !nout && record && *size==11*sizeof(uint32_t));
    ++resets;return reject;
}
static void IOServiceClose(io_connect_t port){assert(port);++closes;}
static void VIMDestroyDrawable(struct VIMCGSDrawable *d)
{++destroys;if(d && d->updates){IOServiceClose(d->updates);d->updates=0;}}
#include "../PassthroughOpenGL/cgs-idle-connections.h"
static struct VIMCGSDrawable drawable(unsigned port)
{struct VIMCGSDrawable d={0};d.updates=port;d.accelerator_surface=1;return d;}
int main(void)
{
    struct VIMContext c={0};assert(!VIMTakeIdleDrawableConnection(&c));
    struct VIMCGSDrawable a=drawable(11),b=drawable(12),overflow=drawable(13);
    VIMRecycleDrawable(&c,&a);VIMRecycleDrawable(&c,&b);VIMRecycleDrawable(&c,&overflow);
#if VIM_REUSE_IDLE_DRAWABLE_CONNECTIONS
    assert(resets==2 && closes==1 && destroys==3 && !a.updates && !b.updates);
    assert(c.idle_drawable_connection_count==2);
    assert(VIMTakeIdleDrawableConnection(&c)==12 && !c.idle_drawable_connections[1]);
    assert(VIMTakeIdleDrawableConnection(&c)==11 && !c.idle_drawable_connections[0]);
    assert(!VIMTakeIdleDrawableConnection(&c));IOServiceClose(12);IOServiceClose(11);
    unsigned old_resets=resets,old_closes=closes;
    for(unsigned i=0;i<5;++i){
        struct VIMCGSDrawable d=drawable(20+i);
        if(i==0)d.locked=1;else if(i==1)d.backing=1;else if(i==2)d.framebuffer=1;
        else if(i==3)d.vram=1;else d.accelerator_surface=0;
        VIMRecycleDrawable(&c,&d);assert(!c.idle_drawable_connection_count);
    }
    assert(resets==old_resets && closes==old_closes+5);
    reject=1;struct VIMCGSDrawable failed=drawable(30);VIMRecycleDrawable(&c,&failed);
    assert(!c.idle_drawable_connection_count && closes==old_closes+6 && resets==old_resets+1);
    reject=0;struct VIMCGSDrawable final=drawable(31);VIMRecycleDrawable(&c,&final);
    assert(c.idle_drawable_connection_count==1);old_closes=closes;
    VIMCloseIdleDrawableConnections(&c);assert(closes==old_closes+1);
#else
    assert(!resets && closes==3 && destroys==3 && !c.idle_drawable_connection_count);
#endif
    VIMRecycleDrawable(&c,NULL);VIMCloseIdleDrawableConnections(&c);
    assert(!c.idle_drawable_connection_count && !VIMTakeIdleDrawableConnection(&c));
    puts("PASS actual GLI idle connection bounds, eligibility, reset failure and teardown");return 0;
}
