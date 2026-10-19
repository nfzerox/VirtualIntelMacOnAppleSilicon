// USB-only privileged service. The VM, disks, display and app stay unprivileged.
#import "VIMUSBProtocol.h"
#import "VIMUSBDevicePolicy.h"
#import "VIMUSBTrust.h" // generated from the actual selected signing identity
#import <Security/Security.h>
#import <IOKit/IOKitLib.h>
#import <os/log.h>
#include <libusb.h>
#include <usbredirhost.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/proc_info.h>
#include <libproc.h>
#include <sys/un.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

static NSMutableSet<NSNumber *> *busyLocations;
static NSObject *ownershipLock;

static id Property(io_registry_entry_t entry,CFStringRef key) {
    return CFBridgingRelease(IORegistryEntryCreateCFProperty(entry,key,NULL,0));
}
#import "VIMUSBPolicyProbe.h"
static BOOL NumberInRange(id value,uint64_t maximum) {
    return [value isKindOfClass:NSNumber.class]&&[value longLongValue]>0&&[value unsignedLongLongValue]<=maximum;
}
static NSDictionary *ResolveDevice(NSDictionary *identity,NSError **error) {
    if(!NumberInRange(identity[@"LocationID"],UINT32_MAX)||!NumberInRange(identity[@"VendorID"],UINT16_MAX)||!NumberInRange(identity[@"ProductID"],UINT16_MAX)) {
        *error=VIMUSBError(@"The USB device identity is invalid.");return nil;
    }
    io_iterator_t iterator=0;NSDictionary *resolved=nil;
    if(IOServiceGetMatchingServices(MACH_PORT_NULL,IOServiceMatching("IOUSBHostDevice"),&iterator)!=KERN_SUCCESS) {
        *error=VIMUSBError(@"USB devices are unavailable.");return nil;
    }
    for(io_service_t device;(device=IOIteratorNext(iterator));IOObjectRelease(device)) {
        if([Property(device,CFSTR("locationID")) unsignedIntValue]!=[identity[@"LocationID"] unsignedIntValue]||
           ![Property(device,CFSTR("idVendor")) isEqual:identity[@"VendorID"]]||
           ![Property(device,CFSTR("idProduct")) isEqual:identity[@"ProductID"]])continue;
        if(VIMUSBIsInternal(device)||[Property(device,CFSTR("bDeviceClass")) integerValue]==9) {
            *error=VIMUSBError(@"Built-in USB devices and hubs cannot be assigned to a virtual Mac.");continue;
        }
        NSMutableSet *disks=[NSMutableSet new];io_iterator_t descendants=0;
        if(IORegistryEntryCreateIterator(device,kIOServicePlane,kIORegistryIterateRecursively,&descendants)!=KERN_SUCCESS) {
            *error=VIMUSBError(@"The USB device’s storage could not be checked.");continue;
        }
        for(io_registry_entry_t child;(child=IOIteratorNext(descendants));IOObjectRelease(child)) {
            if(IOObjectConformsTo(child,"IOMedia")) {
                id name=Property(child,CFSTR("BSD Name"));
                if([name isKindOfClass:NSString.class])[disks addObject:[@"/dev/" stringByAppendingString:name]];
            }
        }
        IOObjectRelease(descendants);
        // Independently refuse all mounted storage. The unprivileged app may
        // normally eject a selected disk, but root never unmounts or opens one.
        struct statfs *mounts=NULL;int count=getmntinfo(&mounts,MNT_NOWAIT);BOOL mounted=count<=0;
        for(int i=0;i<count;i++)if([disks containsObject:@(mounts[i].f_mntfromname)])mounted=YES;
        if(mounted){*error=VIMUSBError(@"Eject this device’s disks on this Mac before connecting it to the virtual Mac.");continue;}
        NSNumber *address=Property(device,CFSTR("USB Address"));
        if(!NumberInRange(address,127)){*error=VIMUSBError(@"The USB device was disconnected.");continue;}
        resolved=@{@"LocationID":identity[@"LocationID"],@"VendorID":identity[@"VendorID"],@"ProductID":identity[@"ProductID"],
            @"Bus":@([identity[@"LocationID"] unsignedIntValue]>>24),@"Address":address};
    }
    IOObjectRelease(iterator);
    if(!resolved&&!*error)*error=VIMUSBError(@"The USB device was disconnected or changed. Reconnect it and try again.");
    return resolved;
}

@interface VIMUSBWorker : NSObject {
@public int _socket;
}
@property NSDictionary *identity;
@property NSFileHandle *listener;
@property pid_t childPID;
@property pid_t clientPID;
@property uid_t clientUID;
@property BOOL cancelled;
@property BOOL captured;
@property NSError *failure;
@property NSString *transportFailure;
@property NSString *backendFailure;
@property NSUInteger warningCount;
@property BOOL traceStorageMetadata;
@property dispatch_semaphore_t ready;
- (void)run;
- (void)cancel;
@end

static _Thread_local void *currentUSBWorker;
static void USBBackendLog(libusb_context *context,enum libusb_log_level level,const char *message) {
    (void)context;
    if(level<=LIBUSB_LOG_LEVEL_WARNING&&currentUSBWorker) {
        VIMUSBWorker *worker=(__bridge VIMUSBWorker *)currentUSBWorker;
        NSString *detail=[@(message) stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
        if(worker.backendFailure.length<2048)worker.backendFailure=worker.backendFailure?[worker.backendFailure stringByAppendingFormat:@"; %@",detail]:detail;
    }
}
static void USBLog(void *context,int level,const char *message) {
    VIMUSBWorker *worker=(__bridge VIMUSBWorker *)context;
    if(worker.traceStorageMetadata&&level==usbredirparser_info&&
       (!strncmp(message,"usbredirhost: UAS ",sizeof("usbredirhost: UAS ")-1)||
        !strncmp(message,"usbredirhost: BOT ",sizeof("usbredirhost: BOT ")-1))) {
        os_log(OS_LOG_DEFAULT,"USB metadata: %{public}s",message);return;
    }
    if(level<=usbredirparser_warning) {
        // Only usbredir's bounded warning/error descriptions, never USB data.
        @synchronized(worker){
            NSString *detail=@(message);
            worker.transportFailure=detail;
            // Exceptional metadata only, bounded per connection, never USB
            // payloads. Retain enough context to diagnose an actual failed
            // guest mount without enabling packet-level tracing.
            if(worker.warningCount++<8)os_log(OS_LOG_DEFAULT,"USB transport: %{public}@",[detail substringToIndex:MIN(detail.length,1024)]);
        }
    }
}
static int USBRead(void *context,uint8_t *bytes,int count) {
    VIMUSBWorker *worker=(__bridge VIMUSBWorker *)context;
    ssize_t result=recv(worker->_socket,bytes,count,0);
    if(result<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))return 0;
    return result>0?(int)result:-1;
}
static int USBWrite(void *context,uint8_t *bytes,int count) {
    VIMUSBWorker *worker=(__bridge VIMUSBWorker *)context;
    ssize_t result=send(worker->_socket,bytes,count,0);
    if(result<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))return 0;
    return result>0?(int)result:-1;
}
@implementation VIMUSBWorker
- (instancetype)init {if((self=[super init])){_socket=-1;_ready=dispatch_semaphore_create(0);}return self;}
- (void)cancel {@synchronized(self){self.cancelled=YES;}}
- (BOOL)isCancelled {@synchronized(self){return self.cancelled;}}
- (void)run {
    @autoreleasepool {
    libusb_context *usb=NULL;libusb_device_handle *handle=NULL;struct usbredirhost *host=NULL;NSError *failure=nil;NSDictionary *device=nil;
    int listener=self.listener.fileDescriptor;
    struct sockaddr_un local;socklen_t length=sizeof(local);int type=0;socklen_t optionLength=sizeof(type);
    if(getsockname(listener,(struct sockaddr *)&local,&length)||local.sun_family!=AF_UNIX||
       getsockopt(listener,SOL_SOCKET,SO_TYPE,&type,&optionLength)||type!=SOCK_STREAM) {
        failure=VIMUSBError(@"USB requires a local stream socket.");goto finished;
    }
    // Darwin Unix sockets reject SO_ACCEPTCONN with ENOPROTOOPT, including on
    // a valid listener. A successful accept below proves passive/listening state
    // before any device capture. Keep it nonblocking even if the peer vanishes.
    fcntl(listener,F_SETFL,O_NONBLOCK);
    // Bound accept without blocking the service or its invalidation handlers.
    for(int ticks=0;ticks<300&&!self.isCancelled;ticks++) {
        struct pollfd pending={listener,POLLIN,0};int available=poll(&pending,1,100);
        if(available<0&&errno==EINTR)continue;
        if(available>0&&(pending.revents&POLLIN)){_socket=accept(listener,NULL,NULL);break;}
        if(available<0||pending.revents&(POLLHUP|POLLERR|POLLNVAL))break;
    }
    if(_socket<0){failure=VIMUSBError(@"The virtual Mac did not connect to its USB service. Try connecting the device again.");goto finished;}
    uid_t uid;gid_t gid;pid_t peer=0;optionLength=sizeof(peer);struct proc_bsdinfo child;
    if(getpeereid(_socket,&uid,&gid)||uid!=self.clientUID||getsockopt(_socket,SOL_LOCAL,LOCAL_PEERPID,&peer,&optionLength)||peer!=self.childPID||
       proc_pidinfo(peer,PROC_PIDTBSDINFO,0,&child,sizeof(child))!=sizeof(child)||child.pbi_uid!=uid||child.pbi_ppid!=(uint32_t)self.clientPID) {
        failure=VIMUSBError(@"The USB connection does not belong to this app’s virtual Mac.");goto finished;
    }
    int noSignal=1;setsockopt(_socket,SOL_SOCKET,SO_NOSIGPIPE,&noSignal,sizeof(noSignal));fcntl(_socket,F_SETFL,O_NONBLOCK);fcntl(_socket,F_SETFD,FD_CLOEXEC);
    device=ResolveDevice(self.identity,&failure);
    if(!device||self.isCancelled)goto finished;
    if(libusb_init(&usb)){failure=VIMUSBError(@"The USB capture service could not start.");goto finished;}
    currentUSBWorker=(__bridge void *)self;
    libusb_set_log_cb(usb,USBBackendLog,LIBUSB_LOG_CB_CONTEXT);
    libusb_set_option(usb,LIBUSB_OPTION_LOG_LEVEL,LIBUSB_LOG_LEVEL_WARNING);
    libusb_device **list=NULL;ssize_t count=libusb_get_device_list(usb,&list);
    if(count>=0) {
        for(ssize_t index=0;index<count;index++) {
            struct libusb_device_descriptor desc;
            if(libusb_get_bus_number(list[index])==[device[@"Bus"] intValue]&&libusb_get_device_address(list[index])==[device[@"Address"] intValue]&&
               !libusb_get_device_descriptor(list[index],&desc)&&desc.idVendor==[device[@"VendorID"] intValue]&&desc.idProduct==[device[@"ProductID"] intValue]) {
                int result=libusb_open(list[index],&handle);
                if(result)failure=VIMUSBError([NSString stringWithFormat:@"The USB device could not be opened (%s).",libusb_error_name(result)]);
                break;
            }
        }
        libusb_free_device_list(list,1);
    }
    if(!handle){if(!failure)failure=VIMUSBError(@"The USB device disappeared before it could be connected.");goto finished;}
    // usbredirhost takes ownership, including on failure. Its normal Darwin
    // libusb capture path runs here as root, never inside the emulator.
    host=usbredirhost_open(usb,handle,USBLog,USBRead,USBWrite,(__bridge void *)self,"Virtual Intel Mac USB",
        self.traceStorageMetadata?usbredirparser_info:usbredirparser_warning,
        self.traceStorageMetadata?usbredirhost_fl_scsi_metadata:0);handle=NULL;
    if(!host){failure=VIMUSBError([NSString stringWithFormat:@"This Mac could not release the USB device. %@%@",self.transportFailure?:@"",self.backendFailure?[@" " stringByAppendingString:self.backendFailure]:@""]);goto finished;}
    @synchronized(self){self.captured=YES;}dispatch_semaphore_signal(self.ready);
    while(!self.isCancelled) {
        struct pollfd input={_socket,POLLIN|(usbredirhost_has_data_to_write(host)?POLLOUT:0),0};
        int available=poll(&input,1,1);
        if(available<0&&errno==EINTR)continue;
        if(available<0||input.revents&(POLLHUP|POLLERR|POLLNVAL))break;
        if((input.revents&POLLIN)&&usbredirhost_read_guest_data(host)<0)break;
        if((input.revents&POLLOUT)&&usbredirhost_write_guest_data(host)<0)break;
        struct timeval timeout={0,0};int result=libusb_handle_events_timeout(usb,&timeout);
        if(result<0&&result!=LIBUSB_ERROR_INTERRUPTED)break;
    }
finished:
    if(failure)NSLog(@"USB connection ended before capture: %@",failure.localizedDescription);
    @synchronized(self){if(!self.captured)self.failure=failure?:VIMUSBError(@"The USB connection was cancelled.");}
    dispatch_semaphore_signal(self.ready);
    if(host)usbredirhost_close(host);else if(handle)libusb_close(handle);
    if(usb)libusb_exit(usb);
    currentUSBWorker=NULL;
    if(_socket>=0){close(_socket);_socket=-1;}
    [self.listener closeFile];self.listener=nil;
    @synchronized(ownershipLock){[busyLocations removeObject:self.identity[@"LocationID"]];}
    }
}
@end

@interface VIMUSBService : NSObject <NSXPCListenerDelegate,VIMUSBHelperProtocol>
@property pid_t clientPID;
@property uid_t clientUID;
@property NSMutableDictionary<NSString *,VIMUSBWorker *> *workers;
@end
@implementation VIMUSBService
- (BOOL)listener:(NSXPCListener *)listener shouldAcceptNewConnection:(NSXPCConnection *)connection {
    if(connection.effectiveUserIdentifier==0)return NO;
    if(@available(macOS 13.0,*)) [connection setCodeSigningRequirement:VIM_USB_CLIENT_REQUIREMENT];
    else return NO; // the shipping host app itself requires macOS 15+
    // An initial check complements the kernel's per-message XPC requirement.
    SecCodeRef code=NULL;SecRequirementRef requirement=NULL;
    NSDictionary *attributes=@{(__bridge NSString *)kSecGuestAttributePid:@(connection.processIdentifier)};
    OSStatus result=SecCodeCopyGuestWithAttributes(NULL,(__bridge CFDictionaryRef)attributes,kSecCSDefaultFlags,&code);
    if(!result)result=SecRequirementCreateWithString((__bridge CFStringRef)VIM_USB_CLIENT_REQUIREMENT,kSecCSDefaultFlags,&requirement);
    if(!result)result=SecCodeCheckValidity(code,kSecCSDefaultFlags,requirement);
    if(code)CFRelease(code);if(requirement)CFRelease(requirement);if(result)return NO;
    VIMUSBService *client=[VIMUSBService new];client.clientPID=connection.processIdentifier;client.clientUID=connection.effectiveUserIdentifier;client.workers=[NSMutableDictionary new];
    connection.exportedInterface=VIMUSBInterface();connection.exportedObject=client;
    // This block owns the controller, not the connection: it guarantees that
    // every worker is cancelled even if the app exits before a reply arrives.
    connection.invalidationHandler=^{@synchronized(client){for(VIMUSBWorker *worker in client.workers.allValues)[worker cancel];[client.workers removeAllObjects];}};
    [connection resume];return YES;
}
- (void)statusWithReply:(void (^)(NSDictionary *))reply {
    NSMutableDictionary *status=[@{@"Protocol":@(VIM_USB_PROTOCOL),@"SourceHash":VIM_USB_SOURCE_HASH} mutableCopy];
    @synchronized(self){for(VIMUSBWorker *worker in self.workers.allValues)@synchronized(worker){if(worker.failure)status[@"LastConnectionError"]=worker.failure.localizedDescription;}}
    reply(status);
}
- (void)checkStorageInterface:(NSDictionary *)identity inUserSession:(BOOL)userSession reply:(void (^)(NSDictionary *,NSError *))reply {
    if(![identity isKindOfClass:NSDictionary.class]||!NumberInRange(identity[@"LocationID"],UINT32_MAX)||
       !NumberInRange(identity[@"VendorID"],UINT16_MAX)||!NumberInRange(identity[@"ProductID"],UINT16_MAX)) {
        reply(nil,VIMUSBError(@"The USB device identity is invalid."));return;
    }
    struct stat console;
    if(stat("/dev/console",&console)||console.st_uid!=self.clientUID) {
        reply(nil,VIMUSBError(@"Sign in to this Mac before checking USB access."));return;
    }
    // Only our root-owned installed executable and numerical device identity.
    // No client-supplied paths, commands, environment or output files.
    NSString *program=[@"/Library/PrivilegedHelperTools/" stringByAppendingString:VIM_USB_SERVICE];
    NSArray *arguments=@[@"--interface-policy-probe",[identity[@"LocationID"] stringValue],
        [identity[@"VendorID"] stringValue],[identity[@"ProductID"] stringValue]];
    uid_t user=self.clientUID;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{@autoreleasepool {
        NSTask *task=[NSTask new];
        task.environment=@{@"PATH":@"/usr/bin:/bin:/usr/sbin:/sbin"};
        task.executableURL=[NSURL fileURLWithPath:userSession?@"/bin/launchctl":program];
        task.arguments=userSession?[@[@"asuser",[@(user) stringValue],program] arrayByAddingObjectsFromArray:arguments]:arguments;
        NSPipe *output=[NSPipe pipe];task.standardOutput=output;
        task.standardInput=NSFileHandle.fileHandleWithNullDevice;task.standardError=NSFileHandle.fileHandleWithNullDevice;
        NSError *failure=nil;
        if(![task launchAndReturnError:&failure]){reply(nil,failure);return;}
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,125*NSEC_PER_SEC),dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{if(task.running)[task terminate];});
        NSData *data=[output.fileHandleForReading readDataToEndOfFile];[task waitUntilExit];
        id decoded=data.length<=65536?[NSJSONSerialization JSONObjectWithData:data options:0 error:NULL]:nil;
        if(![decoded isKindOfClass:NSDictionary.class]) {
            reply(nil,VIMUSBError(@"The bounded USB access check did not return a result."));return;
        }
        NSMutableDictionary *result=[decoded mutableCopy];result[@"WorkerExitStatus"]=@(task.terminationStatus);
        result[@"UserSessionRequested"]=@(userSession);reply(result,nil);
    }});
}
- (void)beginDevice:(NSDictionary *)identity listener:(NSFileHandle *)listener processID:(NSNumber *)processID reply:(void (^)(NSString *,NSError *))reply {
    if(![identity isKindOfClass:NSDictionary.class]||![listener isKindOfClass:NSFileHandle.class]||!NumberInRange(processID,INT_MAX)) {reply(nil,VIMUSBError(@"The USB request is invalid."));return;}
    NSError *failure=nil;NSDictionary *current=ResolveDevice(identity,&failure);if(!current){reply(nil,failure);return;}
    // NSXPC owns the received message's descriptor lifetime. Pin a distinct
    // descriptor before returning the reply and handing work to another queue.
    int listenerFD=dup(listener.fileDescriptor);
    if(listenerFD<0){reply(nil,VIMUSBError(@"The USB socket could not be retained."));return;}
    fcntl(listenerFD,F_SETFD,FD_CLOEXEC);
    NSFileHandle *ownedListener=[[NSFileHandle alloc] initWithFileDescriptor:listenerFD closeOnDealloc:YES];
    @synchronized(ownershipLock){
        if(busyLocations.count>=8||[busyLocations containsObject:identity[@"LocationID"]]){reply(nil,VIMUSBError(@"This device is already connected to a virtual Mac, or all USB connections are in use."));return;}
        [busyLocations addObject:identity[@"LocationID"]];
    }
    VIMUSBWorker *worker=[VIMUSBWorker new];worker.identity=current;worker.listener=ownedListener;
    worker.traceStorageMetadata=[identity[@"TraceStorageMetadata"] isEqual:@YES];
    worker.clientPID=self.clientPID;worker.clientUID=self.clientUID;worker.childPID=processID.intValue;
    NSString *identifier=NSUUID.UUID.UUIDString;@synchronized(self){self.workers[identifier]=worker;}
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{[worker run];});
    reply(identifier,nil);
}
- (void)waitForCapture:(NSString *)sessionID reply:(void (^)(NSError *))reply {
    VIMUSBWorker *worker;@synchronized(self){worker=self.workers[sessionID];}
    if(!worker){reply(VIMUSBError(@"The USB connection has ended."));return;}
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{
        BOOL timedOut=dispatch_semaphore_wait(worker.ready,dispatch_time(DISPATCH_TIME_NOW,32*NSEC_PER_SEC))!=0;
        NSError *failure;@synchronized(worker){failure=timedOut?VIMUSBError(@"USB capture did not finish. Reconnect the device and try again."):worker.failure;}
        if(timedOut)[worker cancel];reply(failure);
    });
}
- (void)endSession:(NSString *)sessionID reply:(void (^)(void))reply {
    @synchronized(self){[self.workers[sessionID] cancel];[self.workers removeObjectForKey:sessionID];}reply();
}
@end
int main(int argc,const char *argv[]) {@autoreleasepool {
    if(geteuid()!=0)return 77;
    if(argc>1) {
        if(!strcmp(argv[1],"--interface-policy-probe"))return VIMUSBRunPolicyProbe(argc,argv);
        return 64;
    }
    signal(SIGPIPE,SIG_IGN);ownershipLock=[NSObject new];busyLocations=[NSMutableSet new];
    NSXPCListener *listener=[[NSXPCListener alloc] initWithMachServiceName:VIM_USB_SERVICE];
    VIMUSBService *service=[VIMUSBService new];listener.delegate=service;[listener resume];
    [[NSRunLoop currentRunLoop] run];
}return 0;}
