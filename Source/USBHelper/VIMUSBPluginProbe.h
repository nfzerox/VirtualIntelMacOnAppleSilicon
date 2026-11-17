#pragma once
#import <IOKit/IOCFPlugIn.h>
#import <IOKit/usb/IOUSBLib.h>

// IOCreatePlugInInterfaceForService collapses every candidate's Start error
// into NoResources. On Big Sur, IOUSBLib accepts an IOUSBHostInterface in Probe
// but returns Unsupported from Start; libusb instead obtains compatibility
// interfaces through its device iterator after capture. Recover the real error
// using the already registered CF plug-in factories and their public API.
// Never reinterpret NoResources itself as permission or supported access.
static IOReturn VIMUSBProbeStoragePlugin(io_service_t service,IOCFPlugInInterface ***output,SInt32 *score) {
    IOReturn result=IOCreatePlugInInterfaceForService(service,kIOUSBInterfaceUserClientTypeID,
        kIOCFPlugInInterfaceID,output,score);
    if(result!=kIOReturnNoResources)return result;
    CFArrayRef factories=CFPlugInFindFactoriesForPlugInType(kIOUSBInterfaceUserClientTypeID);
    BOOL matched=NO;
    for(CFIndex i=0;factories&&i<CFArrayGetCount(factories);++i) {
        CFUUIDRef factory=(CFUUIDRef)CFArrayGetValueAtIndex(factories,i);
        IUnknownVTbl **unknown=(IUnknownVTbl **)CFPlugInInstanceCreate(NULL,factory,kIOUSBInterfaceUserClientTypeID);
        if(!unknown)continue;
        IOCFPlugInInterface **plugin=NULL;
        (*unknown)->QueryInterface(unknown,CFUUIDGetUUIDBytes(kIOCFPlugInInterfaceID),(LPVOID *)&plugin);
        (*unknown)->Release(unknown);
        if(!plugin)continue;
        SInt32 candidateScore=0;
        if((*plugin)->Probe(plugin,NULL,service,&candidateScore)==kIOReturnSuccess) {
            IOReturn started=(*plugin)->Start(plugin,NULL,service);
            if(started==kIOReturnSuccess){
                *output=plugin;*score=candidateScore;result=started;break;
            }
            // Any genuine error wins over Unsupported; a later unsupported
            // factory must never hide a denied/busy/native failure.
            if(!matched||result==kIOReturnUnsupported)result=started;
            matched=YES;
        }
        (*plugin)->Release(plugin);
    }
    if(factories)CFRelease(factories);
    return result;
}
