#import "VIMHostUSB.h"
#import "VIMLibraryLocation.h"
#import "VIMUSBClient.h"
@import DiskArbitration;
#include <sys/mount.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

static NSError *USBReleaseError(NSString *text) {return [NSError errorWithDomain:@"VirtualIntelMac.USB" code:1 userInfo:@{NSLocalizedDescriptionKey:text}];}
static void USBRegistryChanged(void *context,io_iterator_t iterator) {
    (void)context;
    for(io_object_t device;(device=IOIteratorNext(iterator));IOObjectRelease(device)){}
    [NSNotificationCenter.defaultCenter postNotificationName:@"VIMHostUSBChanged" object:nil];
}
void VIMStartHostUSBNotifications(void) {
    static dispatch_once_t once;
    dispatch_once(&once,^{
        static IONotificationPortRef port;
        static io_iterator_t added,removed;
        port=IONotificationPortCreate(MACH_PORT_NULL);if(!port)return;
        IONotificationPortSetDispatchQueue(port,dispatch_get_main_queue());
        IOServiceAddMatchingNotification(port,kIOFirstMatchNotification,IOServiceMatching("IOUSBHostDevice"),USBRegistryChanged,NULL,&added);
        IOServiceAddMatchingNotification(port,kIOTerminatedNotification,IOServiceMatching("IOUSBHostDevice"),USBRegistryChanged,NULL,&removed);
        USBRegistryChanged(NULL,added);USBRegistryChanged(NULL,removed);
    });
}
@interface VIMUSBRelease : NSObject
@property NSArray<NSString *> *disks;
@property NSUInteger index;
@property BOOL force;
@property DASessionRef session;
@property(copy) void (^completion)(NSError *);
- (void)next;
@end
static void DiskReleased(DADiskRef disk,DADissenterRef dissenter,void *context) {
    VIMUSBRelease *operation=CFBridgingRelease(context);
    if(dissenter){operation.completion(USBReleaseError(@"This Mac is still using the device. Close apps that use it, or choose Force Eject if you’re sure it has no unsaved work."));return;}
    [operation next];
}
@implementation VIMUSBRelease
- (void)next {
    if(self.index==self.disks.count){self.completion(nil);return;}
    DADiskRef disk=DADiskCreateFromBSDName(NULL,self.session,self.disks[self.index++].UTF8String);
    if(!disk){self.completion(USBReleaseError(@"The device changed or was disconnected. Reconnect it and try again."));return;}
    DADiskUnmount(disk,kDADiskUnmountOptionWhole|(self.force?kDADiskUnmountOptionForce:0),DiskReleased,(void *)CFBridgingRetain(self));CFRelease(disk);
}
- (void)dealloc {if(_session){DASessionSetDispatchQueue(_session,NULL);CFRelease(_session);}}
@end

NSDictionary *VIMHostUSBStorageInfo(NSDictionary *device,NSArray<NSURL *> *protectedURLs,NSError **error) {
    // Re-resolve the current device, not a BSD name saved when the menu opened.
    io_iterator_t devices=0;io_service_t selected=0;
    if(IOServiceGetMatchingServices(MACH_PORT_NULL,IOServiceMatching("IOUSBHostDevice"),&devices)==KERN_SUCCESS) {
        for(io_service_t service;(service=IOIteratorNext(devices));IOObjectRelease(service)) {
            NSNumber *location=CFBridgingRelease(IORegistryEntryCreateCFProperty(service,CFSTR("locationID"),NULL,0));
            NSNumber *vendor=CFBridgingRelease(IORegistryEntryCreateCFProperty(service,CFSTR("idVendor"),NULL,0));
            NSNumber *product=CFBridgingRelease(IORegistryEntryCreateCFProperty(service,CFSTR("idProduct"),NULL,0));
            if([location unsignedIntValue]==[device[@"LocationID"] unsignedIntValue]&&[vendor isEqual:device[@"VendorID"]]&&[product isEqual:device[@"ProductID"]]){selected=service;IOObjectRetain(selected);}
        }IOObjectRelease(devices);
    }
    if(!selected){if(error)*error=USBReleaseError(@"The device is no longer connected. Reconnect it and try again.");return nil;}
    NSMutableArray *names=[NSMutableArray array];NSMutableSet *allMedia=[NSMutableSet new];io_iterator_t descendants=0;BOOL storageInterface=NO;
    if(IORegistryEntryCreateIterator(selected,kIOServicePlane,kIORegistryIterateRecursively,&descendants)==KERN_SUCCESS) {
        for(io_registry_entry_t media;(media=IOIteratorNext(descendants));IOObjectRelease(media)) {
            if(IOObjectConformsTo(media,"IOUSBHostInterface")) {
                id cls=CFBridgingRelease(IORegistryEntryCreateCFProperty(media,CFSTR("bInterfaceClass"),NULL,0));
                if([cls unsignedIntValue]==8)storageInterface=YES;
            }
            if(!IOObjectConformsTo(media,"IOMedia"))continue;
            id whole=CFBridgingRelease(IORegistryEntryCreateCFProperty(media,CFSTR("Whole"),NULL,0));
            NSString *name=CFBridgingRelease(IORegistryEntryCreateCFProperty(media,CFSTR("BSD Name"),NULL,0));
            if([name isKindOfClass:NSString.class])[allMedia addObject:[@"/dev/" stringByAppendingString:name]];
            NSRegularExpression *pattern=[NSRegularExpression regularExpressionWithPattern:@"^disk[0-9]+$" options:0 error:NULL];
            if([whole boolValue]&&[name isKindOfClass:NSString.class]&&[pattern numberOfMatchesInString:name options:0 range:NSMakeRange(0,name.length)]&&![names containsObject:name])[names addObject:name];
        }IOObjectRelease(descendants);
    } else {
        IOObjectRelease(selected);
        if(error)*error=USBReleaseError(@"This Mac couldn’t check the USB device. Reconnect it and try again.");return nil;
    }IOObjectRelease(selected);
    struct statfs *mounts=NULL;int count=getmntinfo(&mounts,MNT_NOWAIT);
    if(count<=0){if(error)*error=USBReleaseError(@"This Mac’s mounted disks could not be checked. Try again shortly.");return nil;}
    NSMutableArray *mounted=[NSMutableArray new];BOOL protected=NO;
    for(int i=0;i<count;i++)if([allMedia containsObject:@(mounts[i].f_mntfromname)]) {
        NSString *path=@(mounts[i].f_mntonname);[mounted addObject:path];
        if([path isEqual:@"/"])protected=YES;
        for(NSURL *URL in protectedURLs) {
            NSURL *volume=nil;[URL getResourceValue:&volume forKey:NSURLVolumeURLKey error:nil];
            NSString *resolved=URL.URLByResolvingSymlinksInPath.path;
            if([volume.URLByResolvingSymlinksInPath.path isEqual:path]||[resolved isEqual:path]||[resolved hasPrefix:[path stringByAppendingString:@"/"]])protected=YES;
        }
    }
    return @{@"Disks":names,@"MountedVolumes":mounted,@"Protected":@(protected),@"StorageInterface":@(storageInterface)};
}
void VIMHostRequestUSBStorageAccess(NSDictionary *device,NSArray<NSURL *> *protectedURLs,void (^completion)(NSError *)) {
    // A launchd root helper does not acquire removable-volume privacy consent
    // merely by being root. The authenticated user-session plugin preflight
    // requests the actual IOKit permission without opening any raw disk/file.
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
        NSMutableArray *protected=[NSMutableArray arrayWithObjects:NSBundle.mainBundle.bundleURL,VIMLibraryLocation.rootURL,nil];
        [protected addObjectsFromArray:protectedURLs];
        NSError *failure=nil;NSDictionary *info=VIMHostUSBStorageInfo(device,protected,&failure);
        if(info&&[info[@"Protected"] boolValue])failure=USBReleaseError(@"This device contains Virtual Intel Mac or its library and cannot be connected. Move those files to another drive first.");
        dispatch_async(dispatch_get_main_queue(),^{
            // A blank, unmounted or recently reset USB drive may have an
            // interface but no IOMedia yet. Consent must not depend on disks.
            if(failure||(![info[@"StorageInterface"] boolValue]&&![info[@"Disks"] count])){completion(failure);return;}
            // IOKit's storage-interface policy is distinct from raw-file FDA.
            // A launchd daemon with a self-signed identity cannot be attributed
            // through TCC's same-Team-ID heuristic. Request normal scoped consent
            // in the authenticated GUI user's audit session BEFORE unmounting.
            // This creates/releases a plugin only; capture/data stay on the
            // existing daemon -> usbredir -> QEMU path after consent.
            VIMUSBClient *access=[VIMUSBClient new];
            NSDictionary *identity=@{@"LocationID":device[@"LocationID"],@"VendorID":device[@"VendorID"],@"ProductID":device[@"ProductID"]};
            [access checkStorageInterface:identity inUserSession:YES completion:^(NSDictionary *result,NSError *error){
                (void)access; // own the XPC client through the bounded reply
                if(error){completion(error);return;}
                if(![result[@"Found"] boolValue]){completion(USBReleaseError(@"The USB drive was disconnected. Reconnect it and try again."));return;}
                if(![result[@"AuditAvailable"] boolValue]||[result[@"AuditUID"] unsignedIntValue]!=getuid()||
                   [result[@"EffectiveUID"] unsignedIntValue]!=0||![result[@"UserSessionRequested"] boolValue]) {
                    completion(USBReleaseError(@"USB access could not be checked in your signed-in session. Sign in to this Mac and try again."));return;
                }
                NSArray *interfaces=[result[@"StorageInterfaces"] isKindOfClass:NSArray.class]?result[@"StorageInterfaces"]:nil;
                if(!interfaces.count||[result[@"WorkerExitStatus"] integerValue]!=0) {
                    completion(USBReleaseError(@"The USB drive changed while checking access. Reconnect it and try again."));return;
                }
                for(NSDictionary *interface in interfaces)if(![interface isKindOfClass:NSDictionary.class]||![interface[@"PluginCreated"] boolValue]) {
                    uint32_t status=[interface[@"IOReturn"] unsignedIntValue];
                    BOOL denied=status==kIOReturnNotPermitted||status==kIOReturnNotPrivileged;
                    completion([NSError errorWithDomain:@"VirtualIntelMac.USB" code:status userInfo:@{
                        NSLocalizedDescriptionKey:denied?@"Allow access to this USB drive":@"This Mac couldn’t open the USB interface",
                        NSLocalizedRecoverySuggestionErrorKey:denied?@"Allow Removable Volumes for Virtual Intel Mac USB Helper in System Settings → Privacy & Security → Files and Folders, then try again.":@"Disconnect it from other virtual machines and close apps using it. If that doesn’t help, unplug the device, reconnect it, and try again. macOS did not report a denied privacy permission."}]);return;
                }
                completion(nil);
            }];
        });
    });
}
void VIMHostReleaseUSBDisks(NSDictionary *device,NSArray<NSURL *> *protectedURLs,BOOL force,void (^completion)(NSError *)) {
    // Callers must supply an authorization decision, but never a BSD disk name.
    // Recheck all app/library locations immediately before the destructive edge.
    NSMutableArray *protected=[NSMutableArray arrayWithObject:NSBundle.mainBundle.bundleURL];
    [protected addObject:VIMLibraryLocation.rootURL];[protected addObjectsFromArray:protectedURLs];
    NSError *error=nil;NSDictionary *info=VIMHostUSBStorageInfo(device,protected,&error);
    if(!info||[info[@"Protected"] boolValue]){completion(error?:USBReleaseError(@"This device contains Virtual Intel Mac or its library and cannot be unmounted. Move the app and library to another drive first."));return;}
    NSArray *names=info[@"Disks"];
    if(!names.count){completion(nil);return;}
    VIMUSBRelease *operation=[VIMUSBRelease new];operation.disks=names;operation.force=force;operation.completion=completion;operation.session=DASessionCreate(NULL);
    if(!operation.session){completion(USBReleaseError(@"This Mac’s disk service is unavailable. Try again shortly."));return;}
    DASessionSetDispatchQueue(operation.session,dispatch_get_main_queue());[operation next];
}
