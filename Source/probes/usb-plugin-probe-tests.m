// Finite candidate/error/lifetime tests for the actual helper implementation.
// Fake CF factories only; no USB device, authorization or privacy operation.
#import <Foundation/Foundation.h>
#import <IOKit/IOCFPlugIn.h>
#import <IOKit/usb/IOUSBLib.h>

typedef struct {
    IOCFPlugInInterface *table;
    ULONG references;
    IOReturn probe, start;
} Candidate;
static Candidate candidates[3];
static CFUUIDRef identifiers[3];
static CFArrayRef factories;
static IOReturn initialResult;
static unsigned creations;

static ULONG AddRef(void *pointer) {return ++((Candidate *)pointer)->references;}
static ULONG Release(void *pointer) {
    Candidate *candidate=pointer;NSCAssert(candidate->references>0,@"balanced ownership");
    return --candidate->references;
}
static HRESULT Query(void *pointer,REFIID type,LPVOID *out) {
    (void)type;*out=pointer;AddRef(pointer);return S_OK;
}
static IOReturn Probe(void *pointer,CFDictionaryRef properties,io_service_t service,SInt32 *score) {
    (void)properties;(void)service;*score=7;return ((Candidate *)pointer)->probe;
}
static IOReturn Start(void *pointer,CFDictionaryRef properties,io_service_t service) {
    (void)properties;(void)service;return ((Candidate *)pointer)->start;
}
static IOCFPlugInInterface table={.QueryInterface=Query,.AddRef=AddRef,.Release=Release,.Probe=Probe,.Start=Start};
static kern_return_t NativeCreate(io_service_t service,CFUUIDRef type,CFUUIDRef interface,IOCFPlugInInterface ***out,SInt32 *score) {
    (void)service;(void)type;(void)interface;
    if(initialResult==0){AddRef(&candidates[0]);*out=(IOCFPlugInInterface **)&candidates[0];*score=7;}
    return initialResult;
}
static CFArrayRef FindFactories(CFUUIDRef type) {(void)type;return CFRetain(factories);}
static void *InstanceCreate(CFAllocatorRef allocator,CFUUIDRef factory,CFUUIDRef type) {
    (void)allocator;(void)type;
    for(unsigned i=0;i<3;i++)if(CFEqual(factory,identifiers[i])) {
        creations++;AddRef(&candidates[i]);return &candidates[i];
    }
    return NULL;
}
#define IOCreatePlugInInterfaceForService NativeCreate
#define CFPlugInFindFactoriesForPlugInType FindFactories
#define CFPlugInInstanceCreate InstanceCreate
#import "../USBHelper/VIMUSBPluginProbe.h"
#undef IOCreatePlugInInterfaceForService
#undef CFPlugInFindFactoriesForPlugInType
#undef CFPlugInInstanceCreate

static void Check(const char *name,IOReturn initial,const IOReturn *errors,unsigned count,IOReturn expected,BOOL match) {
    creations=0;initialResult=initial;
    for(unsigned i=0;i<3;i++)candidates[i]=(Candidate){.table=&table,.probe=match?0:kIOReturnBadArgument,.start=i<count?errors[i]:0};
    factories=CFArrayCreate(NULL,(const void **)identifiers,count,&kCFTypeArrayCallBacks);
    IOCFPlugInInterface **plugin=NULL;SInt32 score=0;
    IOReturn result=VIMUSBProbeStoragePlugin(1,&plugin,&score);
    NSCAssert(result==expected,@"%s returned 0x%x instead of 0x%x",name,result,expected);
    NSCAssert((plugin!=NULL)==(expected==0),@"Only successful Start publishes a plug-in");
    if(plugin){NSCAssert(score==7,@"Selected score");(*plugin)->Release(plugin);}
    for(unsigned i=0;i<3;i++)NSCAssert(candidates[i].references==0,@"No leaked/released-live candidates");
    if(initial!=kIOReturnNoResources)NSCAssert(creations==0,@"No reinterpretation of an existing precise error");
    CFRelease(factories);printf("PASS %s\n",name);
}
int main(void) {@autoreleasepool {
    for(unsigned i=0;i<3;i++)identifiers[i]=CFUUIDCreate(NULL);
    const IOReturn unsupported[]={kIOReturnUnsupported,kIOReturnUnsupported};
    const IOReturn deniedLater[]={kIOReturnUnsupported,kIOReturnNotPrivileged,kIOReturnUnsupported};
    const IOReturn deniedFirst[]={kIOReturnNotPermitted,kIOReturnUnsupported};
    const IOReturn busy[]={kIOReturnBusy,kIOReturnUnsupported};
    const IOReturn success[]={kIOReturnUnsupported,kIOReturnSuccess};
    Check("initial successful plug-in unchanged",0,NULL,0,0,YES);
    Check("precise initial denial preserved",kIOReturnNotPermitted,NULL,0,kIOReturnNotPermitted,YES);
    Check("all matching factories unsupported",kIOReturnNoResources,unsupported,2,kIOReturnUnsupported,YES);
    Check("later denial wins",kIOReturnNoResources,deniedLater,3,kIOReturnNotPrivileged,YES);
    Check("earlier denial preserved",kIOReturnNoResources,deniedFirst,2,kIOReturnNotPermitted,YES);
    Check("busy is not unsupported",kIOReturnNoResources,busy,2,kIOReturnBusy,YES);
    Check("successful candidate retained for caller",kIOReturnNoResources,success,2,0,YES);
    Check("no factories is not unsupported",kIOReturnNoResources,NULL,0,kIOReturnNoResources,YES);
    Check("nonmatching factories are not unsupported",kIOReturnNoResources,unsupported,2,kIOReturnNoResources,NO);
    for(unsigned i=0;i<3;i++)CFRelease(identifiers[i]);
    return 0;
} }
