/* Darwin8's native Rosetta GL namespace lacks IOMasterPort and
 * host_get_io_master. Call the actual subject IOKit API through Apple's BDL,
 * not a guessed Mach message or a donated framework. The native BDL19.3 i386
 * disassembly establishes descriptor {name,address}, argument count and vector.
 * Only the single out port is subject-endian; argument words and return status
 * are marshalled by BDL. Normal Intel processes call their native IOKit API. */
#ifndef VIM_TIGER_ROSETTA_IOKIT_H
#define VIM_TIGER_ROSETTA_IOKIT_H
#include <dlfcn.h>
#include <pthread.h>
typedef uint32_t (*VIMTigerSubjectLookup)(const char *);
struct VIMTigerSubjectFunction {const char *name;uint32_t address;};
typedef uint32_t (*VIMTigerSubjectCall)(struct VIMTigerSubjectFunction *,uint32_t,const uint32_t *);
typedef kern_return_t (*VIMTigerNativeMaster)(mach_port_t,mach_port_t *);
/* Preserve normal two-level IOKit binding. RTLD_DEFAULT alone can miss a
 * framework loaded in a local GL plug-in group. Missing in Rosetta's shim
 * is a weak NULL, not a load failure. */
extern kern_return_t IOMasterPort(mach_port_t,mach_port_t *) __attribute__((weak_import));
static pthread_once_t vim_tiger_master_once=PTHREAD_ONCE_INIT;
static VIMTigerSubjectCall vim_tiger_subject_call;
static uint32_t vim_tiger_subject_master;
static VIMTigerNativeMaster vim_tiger_native_master;
static void VIMTigerResolveMasterPort(void)
{
    vim_tiger_native_master=IOMasterPort;
    if(vim_tiger_native_master)return;
    VIMTigerSubjectLookup lookup=(VIMTigerSubjectLookup)dlsym(RTLD_DEFAULT,"bdl_lookup_subject_symbol");
    vim_tiger_subject_call=(VIMTigerSubjectCall)dlsym(RTLD_DEFAULT,"bdl_call_subject_function_int");
    if(lookup&&vim_tiger_subject_call)vim_tiger_subject_master=lookup("IOMasterPort");
    else {
        vim_tiger_subject_call=NULL;
    }
}
static kern_return_t VIMTigerCopyMasterPort(mach_port_t *master)
{
    pthread_once(&vim_tiger_master_once,VIMTigerResolveMasterPort);
    *master=MACH_PORT_NULL;
    if(vim_tiger_subject_call){
        struct VIMTigerSubjectFunction function={"IOMasterPort",vim_tiger_subject_master};
        if(!function.address)return kIOReturnUnsupported;
        unsigned char port[4]={0};
        uint32_t arguments[]={MACH_PORT_NULL,(uint32_t)(uintptr_t)port};
        kern_return_t error=(kern_return_t)vim_tiger_subject_call(&function,2,arguments);
        if(!error)*master=((uint32_t)port[0]<<24)|((uint32_t)port[1]<<16)|
            ((uint32_t)port[2]<<8)|port[3];
        return error;
    }
    return vim_tiger_native_master?vim_tiger_native_master(MACH_PORT_NULL,master):kIOReturnUnsupported;
}
#endif
