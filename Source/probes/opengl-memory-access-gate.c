/* Own, named-object scratch memory ONLY. Qualify original pointer AND alias,
 * plus resident/faulting neighbours in that same object. This is a CPU-access
 * primitive test, NOT asynchronous GPU rendering or a desktop speed claim. */
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "opengl-memory-gpu-contract.h"
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition=PTHREAD_COND_INITIALIZER;
static int ready;
static mach_timebase_info_data_t scale;
struct Access {volatile unsigned char *at;const char *label;int write;double milliseconds;unsigned value;};
static void *access_memory(void *opaque)
{
    struct Access *p=opaque;
    pthread_mutex_lock(&lock);while(!ready)pthread_cond_wait(&condition,&lock);pthread_mutex_unlock(&lock);
    uint64_t start=mach_absolute_time();
    if(p->write)*p->at=0x75;
    p->value=*p->at;
    p->milliseconds=(mach_absolute_time()-start)*(double)scale.numer/scale.denom/1e6;
    return NULL;
}
int main(int argc,char **argv)
{
    int close_early=argc==3 && !strcmp(argv[2],"--close-while-blocked");
    int map_alias=argc==3 && !strcmp(argv[2],"--kernel-map-while-blocked");
    if((argc!=2 && !close_early && !map_alias) || strcmp(argv[1],"--scratch-only")) {
        fputs("usage: opengl-memory-access-gate --scratch-only [--close-while-blocked|--kernel-map-while-blocked]\n",stderr);return 2;
    }
    printf("process-bits=%u close-while-blocked=%d\n",(unsigned)(sizeof(void *)*8),close_early);
    mach_timebase_info(&scale);mach_vm_size_t size=3*4096;
    memory_object_size_t entry_size=size;mach_port_t entry=MACH_PORT_NULL;
    kern_return_t error=mach_make_memory_entry_64(mach_task_self(),&entry_size,0,
        VM_PROT_READ|VM_PROT_WRITE|MAP_MEM_NAMED_CREATE,&entry,MACH_PORT_NULL);
    mach_vm_address_t original=0,alias=0;
    if(!error)error=mach_vm_map(mach_task_self(),&original,size,0,VM_FLAGS_ANYWHERE,entry,0,FALSE,
        VM_PROT_READ|VM_PROT_WRITE,VM_PROT_READ|VM_PROT_WRITE,VM_INHERIT_NONE);
    if(!error)error=mach_vm_map(mach_task_self(),&alias,size,0,VM_FLAGS_ANYWHERE,entry,0,FALSE,
        VM_PROT_READ|VM_PROT_WRITE,VM_PROT_READ|VM_PROT_WRITE,VM_INHERIT_NONE);
    if(error){fprintf(stderr,"named scratch mapping failed=%x\n",error);return 1;}
    memset((void *)(uintptr_t)original,0x42,(size_t)size);
    volatile unsigned char warm=*(volatile unsigned char *)(uintptr_t)(alias+4096);(void)warm;
    /* Remove the third page's translations before the block. Its later
     * fault tests whether exclusion is wider than the requested middle page. */
    error=mach_vm_protect(mach_task_self(),alias+8192,4096,FALSE,VM_PROT_NONE);
    if(!error)error=mach_vm_protect(mach_task_self(),alias+8192,4096,FALSE,VM_PROT_READ|VM_PROT_WRITE);
    io_service_t service=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("VIMMemoryGate"));
    io_connect_t connection=0;
    if(!error)error=service?IOServiceOpen(service,mach_task_self(),0,&connection):kIOReturnNotFound;
    if(service)IOObjectRelease(service);
    if(error){fprintf(stderr,"open scratch gate failed=%x\n",error);return 1;}
    struct Access readers[]={
        {(void *)(uintptr_t)(original+4096),"original read",0,0,0},
        {(void *)(uintptr_t)(alias+4097),"alias write/read",1,0,0},
        {(void *)(uintptr_t)original,"resident neighbour",0,0,0},
        {(void *)(uintptr_t)(alias+8192),"faulting neighbour",0,0,0}};
    pthread_t threads[4];unsigned made=0;
    for(;made<4;++made)if(pthread_create(threads+made,NULL,access_memory,readers+made))break;
    uint64_t input[]={original+4096,4096};
    error=made==4?IOConnectCallScalarMethod(connection,0,input,2,NULL,NULL):kIOReturnNoResources;
    IOReturn closed=0;
    if(close_early){closed=IOServiceClose(connection);connection=0;}
    pthread_mutex_lock(&lock);ready=1;pthread_cond_broadcast(&condition);pthread_mutex_unlock(&lock);
    IOReturn map_result=0;uint64_t mapped=0;double map_ms=0;
    if(map_alias && !error) {
        uint32_t mapped_count=1;uint64_t map_start=mach_absolute_time();
        map_result=IOConnectCallScalarMethod(connection,VIM_GATE_MAP_ALIAS,NULL,0,&mapped,&mapped_count);
        map_ms=(mach_absolute_time()-map_start)*(double)scale.numer/scale.denom/1e6;
        printf("independent kernel alias mapping result=%x mapped=%llu elapsed_ms=%.3f\n",map_result,mapped,map_ms);
    }
    for(unsigned i=0;i<made;++i)pthread_join(threads[i],NULL);
    uint64_t output[2]={0,0};uint32_t count=2;
    IOReturn restored=connection?IOConnectCallScalarMethod(connection,1,NULL,0,output,&count):closed;
    for(unsigned i=0;i<made;++i)printf("%s elapsed_ms=%.3f value=%02x\n",readers[i].label,readers[i].milliseconds,readers[i].value);
    if(close_early)printf("block=%x early_close=%x; restoration established by actual CPU access, no status reply\n",error,closed);
    else printf("block=%x restore_call=%x restore_result=%llx pending=%llu\n",error,restored,output[1],output[0]);
    int status_ok=close_early?!closed:(!restored && count==2 && !output[0] && !output[1]);
    int qualified=!error && status_ok && made==4 && (!map_alias || (!map_result && mapped==1)) &&
        readers[0].milliseconds>=100 && readers[1].milliseconds>=100 &&
        readers[0].value==0x42 && readers[1].value==0x75 &&
        *(volatile unsigned char *)(uintptr_t)(original+4097)==0x75;
    printf("%s original-pointer/alias exclusion prerequisite; not a GPU or neighbourhood-isolation pass\n",qualified?"PASS":"FAIL");
    if(connection)IOServiceClose(connection);mach_vm_deallocate(mach_task_self(),original,size);
    mach_vm_deallocate(mach_task_self(),alias,size);mach_port_deallocate(mach_task_self(),entry);
    return qualified?0:1;
}
