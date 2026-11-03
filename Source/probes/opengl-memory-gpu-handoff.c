/* Actual own guest pages -> native GPU -> independent kernel completion ->
 * FIRST original/alias CPU pixels. No WindowServer injection/test surface,
 * replacement framework or repairing glReadPixels/Finish. Lab opt-in only. */
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#include "opengl-memory-gpu-contract.h"
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <assert.h>
enum {Width=61,Height=37,Pitch=256,Length=12288};
struct CPURead {volatile unsigned char *pixels;uint32_t expected;int good;double ms;};
static mach_timebase_info_data_t timebase;
static void *first_cpu_read(void *opaque)
{
    struct CPURead *p=opaque;uint64_t start=mach_absolute_time();p->good=1;
    for(unsigned y=0;y<Height;++y)for(unsigned x=0;x<Pitch/4;++x)
        if(((volatile uint32_t*)p->pixels)[y*(Pitch/4)+x]!=(x<Width?p->expected:0xa7a7a7a7u))p->good=0;
    for(unsigned i=Pitch*Height;i<Length;++i)if(p->pixels[i]!=0xa7)p->good=0;
    p->ms=(mach_absolute_time()-start)*(double)timebase.numer/timebase.denom/1e6;return NULL;
}
int main(int argc,char **argv)
{
    int close_early=argc==3 && !strcmp(argv[2],"--close-while-pending");
    if((argc!=2 && !close_early) || strcmp(argv[1],"--scratch-only"))return 2;
    mach_timebase_info(&timebase);
    unsigned char key=0;struct DGContext *c=DGCreateContext(NULL,NULL);
    assert(c && DGMakeCurrent(c,&key,GL_UNSIGNED_BYTE,Width,Height));
    if(!(c->connection->capabilities&DG_CAP_BACKING_COMPLETION)){
        fputs("REFUSED: explicit asynchronous host executor required; no scratch prepared\n",stderr);
        DGDestroyContext(c);return 2;
    }
    char boot[64]={0};size_t boot_size=sizeof(boot);uuid_t boot_uuid;uint64_t namespace_id[2]={0,0};
    assert(!sysctlbyname("kern.bootsessionuuid",boot,&boot_size,NULL,0) &&
        memchr(boot,0,sizeof(boot)) && !uuid_parse(boot,boot_uuid));memcpy(namespace_id,boot_uuid,16);
    printf("process-bits=%u close-while-pending=%d renderer=%s\n",(unsigned)(sizeof(void*)*8),close_early,glGetString(GL_RENDERER));
    assert(strstr((const char*)glGetString(GL_RENDERER),"Intel"));
    memory_object_size_t entry_size=Length;mach_port_t entry=0;mach_vm_address_t original=0,alias=0;
    assert(!mach_make_memory_entry_64(mach_task_self(),&entry_size,0,VM_PROT_READ|VM_PROT_WRITE|MAP_MEM_NAMED_CREATE,&entry,0));
    assert(!mach_vm_map(mach_task_self(),&original,Length,0,VM_FLAGS_ANYWHERE,entry,0,FALSE,
        VM_PROT_READ|VM_PROT_WRITE,VM_PROT_READ|VM_PROT_WRITE,VM_INHERIT_NONE));
    assert(!mach_vm_map(mach_task_self(),&alias,Length,0,VM_FLAGS_ANYWHERE,entry,0,FALSE,
        VM_PROT_READ|VM_PROT_WRITE,VM_PROT_READ|VM_PROT_WRITE,VM_INHERIT_NONE));
    memset((void*)(uintptr_t)original,0xa7,Length);
    io_service_t service=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("VIMMemoryGate"));
    io_connect_t gate=0;assert(service && !IOServiceOpen(service,mach_task_self(),0,&gate));IOObjectRelease(service);
    uint64_t events_before=0;uint32_t event_count=1;
    assert(!IOConnectCallScalarMethod(gate,VIM_GATE_EVENT_COUNTS,NULL,0,&events_before,&event_count) && event_count==1);
    struct VIMGatePages backing={0};size_t bytes=sizeof(backing);uint64_t range[]={original,Length};
    assert(!IOConnectCallMethod(gate,VIM_GATE_PREPARE,range,2,NULL,0,NULL,NULL,&backing,&bytes));
    assert(bytes==sizeof(backing) && backing.length==Length && backing.serial);
    struct DGPacket p=DGPacketNew(c,DG_BACKING_RENDER_BEGIN);p.message.flags=DG_INPUT_ONLY;p.message.bytes=3*sizeof(uint64_t);
    p.data=(void*)backing.pages;p.capacity=p.message.bytes;p.borrowed=1;
    uint64_t *a=p.message.args;a[1]=Length;a[2]=a[7]=Width;a[3]=a[8]=Height;a[4]=Pitch;
    a[10]=UINT64_C(0x7000000000000000)|(uint64_t)(unsigned)getpid();a[11]=a[12]=a[13]=1;a[14]=3;a[15]=backing.serial;
    struct DGReply reply=DGSubmit(c,&p,1);assert(p.valid_reply && !reply.error && reply.result==1);
    glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    p=DGPacketNew(c,DG_BACKING_RENDER_MATERIALIZE);p.message.args[0]=backing.serial;
    reply=DGSubmit(c,&p,1);assert(p.valid_reply && !reply.error && reply.result==1);
    glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT); // later PRIVATE writes must reach real RAM
    p=DGPacketNew(c,DG_BACKING_RENDER_DEFER);p.message.args[0]=backing.serial;
    reply=DGSubmit(c,&p,1);assert(p.valid_reply && !reply.error && reply.result);
    uint64_t ticket=reply.result;
    uint64_t commit[]={backing.serial,ticket,namespace_id[0],namespace_id[1]};
    uint64_t start=mach_absolute_time();IOReturn committed=IOConnectCallScalarMethod(gate,VIM_GATE_COMMIT,commit,4,NULL,NULL);
    double commit_ms=(mach_absolute_time()-start)*(double)timebase.numer/timebase.denom/1e6;
    if(committed){
        fprintf(stderr,"gate commit failed=%x; draining real producer before scratch abort\n",committed);
        p=DGPacketNew(c,DG_BACKING_COMPLETION_WAIT);p.message.context=0;p.message.args[0]=ticket;
        reply=DGSubmit(c,&p,1);int completed=p.valid_reply && !reply.error && reply.result==1;
        if(completed){
            p=DGPacketNew(c,DG_BACKING_COMPLETION_RELEASE);p.message.context=0;p.message.args[0]=ticket;
            DGSubmit(c,&p,1);
        }
        DGDestroyContext(c);uint64_t serial=backing.serial;
        if(completed)IOConnectCallScalarMethod(gate,VIM_GATE_ABORT_DRAINED,&serial,1,NULL,NULL);
        else fputs("No completed ticket: keeping the prepared scratch pin quarantined\n",stderr);
        IOServiceClose(gate);return 1;
    }
    if(close_early){assert(!IOServiceClose(gate));gate=0;}
    struct CPURead reads[]={ {(void*)(uintptr_t)original,0xff0000ffu,0,0}, {(void*)(uintptr_t)alias,0xff0000ffu,0,0} };
    pthread_t threads[2];assert(!pthread_create(threads,NULL,first_cpu_read,reads));
    assert(!pthread_create(threads+1,NULL,first_cpu_read,reads+1));
    uint64_t status[2]={0,0};uint32_t count=2;
    if(gate)assert(!IOConnectCallScalarMethod(gate,1,NULL,0,status,&count) && count==2 && !status[0] && !status[1]);
    for(unsigned i=0;i<2;++i){assert(!pthread_join(threads[i],NULL));assert(reads[i].good);}
    io_connect_t observer=gate;
    if(!observer){
        service=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("VIMMemoryGate"));
        assert(service && !IOServiceOpen(service,mach_task_self(),0,&observer));IOObjectRelease(service);
    }
    uint64_t events_after=0;event_count=1;
    assert(!IOConnectCallScalarMethod(observer,VIM_GATE_EVENT_COUNTS,NULL,0,&events_after,&event_count) &&
        event_count==1 && events_after==events_before+1);
    if(!gate)assert(!IOServiceClose(observer));
    printf("PASS real GPU IRQ + kernel CPU gate: exactly one native event, first original/alias pixels and padding, private later writes, submit_ms=%.3f read_ms=%.3f/%.3f\n",
        commit_ms,reads[0].ms,reads[1].ms);
    if(gate)assert(!IOServiceClose(gate));DGDestroyContext(c);
    mach_vm_deallocate(mach_task_self(),original,Length);mach_vm_deallocate(mach_task_self(),alias,Length);
    mach_port_deallocate(mach_task_self(),entry);return 0;
}
