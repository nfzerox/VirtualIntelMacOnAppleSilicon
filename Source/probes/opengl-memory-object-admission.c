/* Own named objects ONLY. No GPU, WindowServer pointer or desktop memory.
 * Verify the real kernel's admission semantics before enabling async use:
 * reject a partial object, reject a competing alias, admit unrelated objects,
 * then allow the alias only after the first owner has retired its pin.
 */
#include "opengl-memory-gpu-contract.h"
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
enum { Length=12288 };
static io_connect_t open_gate(void)
{
    io_service_t service=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("VIMMemoryGate"));
    io_connect_t c=0;assert(service && !IOServiceOpen(service,mach_task_self(),0,&c));
    IOObjectRelease(service);return c;
}
static IOReturn prepare(io_connect_t c,mach_vm_address_t address,uint64_t length,struct VIMGatePages *out)
{
    uint64_t range[]={address,length};size_t size=sizeof(*out);memset(out,0,size);
    return IOConnectCallMethod(c,VIM_GATE_PREPARE,range,2,NULL,0,NULL,NULL,out,&size);
}
static void retire(io_connect_t c,const struct VIMGatePages *out)
{
    uint64_t serial=out->serial;assert(serial);
    assert(!IOConnectCallScalarMethod(c,VIM_GATE_ABORT_DRAINED,&serial,1,NULL,NULL));
}
int main(void)
{
    mach_port_t entries[2]={0,0};mach_vm_address_t ranges[3]={0,0,0};
    for(unsigned i=0;i<2;++i) {
        memory_object_size_t size=Length;
        assert(!mach_make_memory_entry_64(mach_task_self(),&size,0,
            VM_PROT_READ|VM_PROT_WRITE|MAP_MEM_NAMED_CREATE,entries+i,0));
    }
    for(unsigned i=0;i<3;++i) {
        assert(!mach_vm_map(mach_task_self(),ranges+i,Length,0,VM_FLAGS_ANYWHERE,entries[i==2],0,FALSE,
            VM_PROT_READ|VM_PROT_WRITE,VM_PROT_READ|VM_PROT_WRITE,VM_INHERIT_NONE));
        memset((void *)(uintptr_t)ranges[i],0x42,Length);
    }
    io_connect_t first=open_gate(),alias=open_gate(),independent=open_gate();
    struct VIMGatePages a,b,c;
    IOReturn prefix=prepare(first,ranges[0],4096,&a);assert(prefix==kIOReturnUnsupported);
    IOReturn partial=prepare(first,ranges[0]+4096,4096,&a);
    assert(partial==kIOReturnUnsupported);
    assert(!prepare(first,ranges[0],Length,&a));
    IOReturn busy=prepare(alias,ranges[1],Length,&b);assert(busy==kIOReturnBusy);
    assert(!prepare(independent,ranges[2],Length,&c));
    retire(first,&a);assert(!prepare(alias,ranges[1],Length,&b));
    retire(alias,&b);retire(independent,&c);
    assert(!IOServiceClose(first) && !IOServiceClose(alias) && !IOServiceClose(independent));
    for(unsigned i=0;i<3;++i)assert(!mach_vm_deallocate(mach_task_self(),ranges[i],Length));
    for(unsigned i=0;i<2;++i)assert(!mach_port_deallocate(mach_task_self(),entries[i]));
    printf("PASS %u-bit real kernel whole-object admission: prefix=%x middle=%x alias=%x independent accepted, alias accepted after retirement\n",
        (unsigned)sizeof(void *)*8,prefix,partial,busy);return 0;
}
