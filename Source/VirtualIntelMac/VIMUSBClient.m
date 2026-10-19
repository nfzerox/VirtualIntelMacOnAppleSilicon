#import "VIMUSBClient.h"
#import "VIMQMPConnection.h"
#import "../USBHelper/VIMUSBProtocol.h"
#import <ServiceManagement/ServiceManagement.h>
#import <Security/Authorization.h>
#import <Security/AuthorizationTags.h>
#import <IOKit/usb/IOUSBHostFamilyDefinitions.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <readpassphrase.h>

@interface VIMUSBClient ()
@property NSXPCConnection *connection;
@property NSMutableDictionary<NSString *,NSDictionary *> *sessions;
@property NSMutableDictionary<NSString *,NSString *> *retiringCharacters;
@property BOOL prepared;
@property BOOL preparing;
@property BOOL stopped;
@end

static void InstallUSBHelper(BOOL terminal,void (^completion)(NSError *)) {
    // Standard Authorization Services owns both authentication and installation.
    // The development-only terminal path never stores a password or passes one
    // in argv/environment. Production uses the normal macOS administrator UI.
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
        AuthorizationRef authorization=NULL;OSStatus status=AuthorizationCreate(NULL,kAuthorizationEmptyEnvironment,kAuthorizationFlagDefaults,&authorization);
        AuthorizationItem right={kSMRightBlessPrivilegedHelper,0,NULL,0};AuthorizationRights rights={1,&right};
        char password[1024]={0};const char *user=NSUserName().UTF8String;
        AuthorizationItem items[]={{kAuthorizationEnvironmentUsername,strlen(user),(void *)user,0},
            {kAuthorizationEnvironmentPassword,0,password,0}};
        AuthorizationEnvironment environment={2,items};
        if(!status&&terminal&&!readpassphrase("Administrator password for USB helper: ",password,sizeof(password),RPP_ECHO_OFF|RPP_REQUIRE_TTY))status=errAuthorizationCanceled;
        items[1].valueLength=strlen(password);
        if(!status)status=AuthorizationCopyRights(authorization,&rights,terminal?&environment:kAuthorizationEmptyEnvironment,
            kAuthorizationFlagExtendRights|kAuthorizationFlagPreAuthorize|(terminal?0:kAuthorizationFlagInteractionAllowed),NULL);
        memset_s(password,sizeof(password),0,sizeof(password));
        CFErrorRef blessError=NULL;BOOL blessed=NO;
        if(!status)blessed=SMJobBless(kSMDomainSystemLaunchd,(__bridge CFStringRef)VIM_USB_SERVICE,authorization,&blessError);
        if(authorization)AuthorizationFree(authorization,kAuthorizationFlagDestroyRights);
        NSError *failure=CFBridgingRelease(blessError);
        if(status||!blessed)failure=failure?:[NSError errorWithDomain:NSOSStatusErrorDomain code:status userInfo:@{NSLocalizedDescriptionKey:
            status==errAuthorizationCanceled?@"USB helper setup was cancelled.":@"The USB helper couldn’t be installed. Approve administrator access to connect USB devices; the virtual Mac itself does not need administrator access."}];
        dispatch_async(dispatch_get_main_queue(),^{completion(failure);});
    });
}

@implementation VIMUSBClient
- (instancetype)init {if((self=[super init])){_sessions=[NSMutableDictionary new];_retiringCharacters=[NSMutableDictionary new];}return self;}
- (void)openConnection {
    [self.connection invalidate];
    NSXPCConnection *connection=[[NSXPCConnection alloc] initWithMachServiceName:VIM_USB_SERVICE options:NSXPCConnectionPrivileged];
    connection.remoteObjectInterface=VIMUSBInterface();
    NSString *requirement=NSBundle.mainBundle.infoDictionary[@"SMPrivilegedExecutables"][VIM_USB_SERVICE];
    if(@available(macOS 13.0,*)){if(requirement.length)[connection setCodeSigningRequirement:requirement];}
    __weak VIMUSBClient *weakSelf=self;
    connection.invalidationHandler=^{dispatch_async(dispatch_get_main_queue(),^{weakSelf.prepared=NO;});};
    self.connection=connection;[connection resume];
}
- (void)checkWithCompletion:(void (^)(BOOL valid))completion {
    __block BOOL answered=NO;
    void (^finish)(BOOL)=^(BOOL valid){dispatch_async(dispatch_get_main_queue(),^{if(answered)return;answered=YES;completion(valid);});};
    id<VIMUSBHelperProtocol> proxy=[self.connection remoteObjectProxyWithErrorHandler:^(NSError *error){finish(NO);}];
    [proxy statusWithReply:^(NSDictionary *status){finish([status[@"Protocol"] isEqual:@(VIM_USB_PROTOCOL)]&&
        [status[@"SourceHash"] isEqual:NSBundle.mainBundle.infoDictionary[@"VIMUSBHelperSourceHash"]]);}];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,5*NSEC_PER_SEC),dispatch_get_main_queue(),^{if(!answered){answered=YES;completion(NO);}});
}
+ (void)authorizeUsingTerminalWithCompletion:(void (^)(NSError *))completion {
    if(!NSBundle.mainBundle.infoDictionary[@"VIMUSBHelperSourceHash"]){completion(VIMUSBError(@"Build the complete app with its USB helper before authorizing it."));return;}
    InstallUSBHelper(YES,completion);
}
- (void)prepareWithCompletion:(void (^)(NSError *))completion {
    if(self.stopped){completion(VIMUSBError(@"This virtual Mac has stopped."));return;}
    if(self.prepared){completion(nil);return;}
    if(self.preparing){completion(VIMUSBError(@"USB helper setup is already in progress."));return;}
    if(!NSBundle.mainBundle.infoDictionary[@"VIMUSBHelperSourceHash"]){completion(VIMUSBError(@"This development build does not include USB capture. Open the complete Virtual Intel Mac app."));return;}
    self.preparing=YES;[self openConnection];
    [self checkWithCompletion:^(BOOL valid){
        if(valid){self.preparing=NO;self.prepared=YES;completion(nil);return;}
        InstallUSBHelper(NO,^(NSError *failure){
            if(failure){self.preparing=NO;completion(failure);return;}
            [self openConnection];[self checkWithCompletion:^(BOOL ready){self.preparing=NO;self.prepared=ready;completion(ready?nil:VIMUSBError(@"The USB helper did not become available. Try connecting the device again."));}];
        });
    }];
}
- (void)checkStorageInterface:(NSDictionary *)identity inUserSession:(BOOL)userSession completion:(void (^)(NSDictionary *,NSError *))completion {
    [self prepareWithCompletion:^(NSError *failure){
        if(failure){completion(nil,failure);return;}
        id<VIMUSBHelperProtocol> proxy=[self.connection remoteObjectProxyWithErrorHandler:^(NSError *error){
            dispatch_async(dispatch_get_main_queue(),^{completion(nil,error);});}];
        [proxy checkStorageInterface:identity inUserSession:userSession reply:^(NSDictionary *result,NSError *error){
            dispatch_async(dispatch_get_main_queue(),^{completion(result,error);});}];
    }];
}
- (void)connectDevice:(NSDictionary *)device processID:(pid_t)pid directory:(NSString *)directory control:(VIMQMPConnection *)control completion:(void (^)(NSError *))completion {
    if(!self.prepared||self.stopped){completion(VIMUSBError(@"USB capture is not ready."));return;}
    NSString *identifier=device[@"Identifier"],*character=[NSString stringWithFormat:@"char-%@-%@",identifier,NSUUID.UUID.UUIDString];
    if(self.sessions[identifier]){completion(VIMUSBError(@"This USB device is already connected."));return;}
    NSString *path=[directory stringByAppendingPathComponent:[NSString stringWithFormat:@"usb-%@",NSUUID.UUID.UUIDString]];
    struct sockaddr_un address={0};address.sun_family=AF_UNIX;
    if([path lengthOfBytesUsingEncoding:NSUTF8StringEncoding]>=sizeof(address.sun_path)){completion(VIMUSBError(@"The USB connection path is too long."));return;}
    strlcpy(address.sun_path,path.fileSystemRepresentation,sizeof(address.sun_path));
    int fd=socket(AF_UNIX,SOCK_STREAM,0);
    if(fd<0||bind(fd,(struct sockaddr *)&address,sizeof(address))||chmod(path.fileSystemRepresentation,0600)||listen(fd,1)) {
        if(fd>=0)close(fd);unlink(path.fileSystemRepresentation);completion(VIMUSBError(@"The local USB connection could not be created."));return;
    }
    fcntl(fd,F_SETFD,FD_CLOEXEC);
    NSFileHandle *listener=[[NSFileHandle alloc] initWithFileDescriptor:fd closeOnDealloc:YES];
    __block BOOL completed=NO;__block NSString *rootSession=nil;
    void (^finish)(NSError *)=^(NSError *error){
        if(completed)return;completed=YES;
        [listener closeAndReturnError:NULL];unlink(path.fileSystemRepresentation);
        if(error) {
            id<VIMUSBHelperProtocol> cleanup=[self.connection remoteObjectProxyWithErrorHandler:^(NSError *ignored){}];
            if(rootSession)[cleanup endSession:rootSession reply:^{}];
            [self.sessions removeObjectForKey:identifier];
            [control execute:@"chardev-remove" arguments:@{@"id":character} completion:^(id ignored,NSError *cleanupError){}];
        }
        completion(error);
    };
    id<VIMUSBHelperProtocol> proxy=[self.connection remoteObjectProxyWithErrorHandler:^(NSError *error){dispatch_async(dispatch_get_main_queue(),^{finish(error);});}];
    [proxy beginDevice:@{@"LocationID":device[@"LocationID"],@"VendorID":device[@"VendorID"],@"ProductID":device[@"ProductID"],
        @"TraceStorageMetadata":@([NSProcessInfo.processInfo.arguments containsObject:@"--trace-usb-storage"])}
        listener:listener processID:@(pid) reply:^(NSString *sessionID,NSError *failure){dispatch_async(dispatch_get_main_queue(),^{
        rootSession=sessionID;
        if(failure||self.stopped){finish(failure?:VIMUSBError(@"This virtual Mac has stopped."));return;}
        self.sessions[identifier]=@{@"RootID":sessionID,@"Character":character,@"Path":path};
        [control execute:@"chardev-add" arguments:@{@"id":character,@"backend":@{@"type":@"socket",@"data":@{
            @"addr":@{@"type":@"unix",@"data":@{@"path":path}},@"server":@NO}}} completion:^(id result,NSError *error){
            if(error){[proxy statusWithReply:^(NSDictionary *status){dispatch_async(dispatch_get_main_queue(),^{
                finish([status[@"LastConnectionError"] isKindOfClass:NSString.class]?VIMUSBError(status[@"LastConnectionError"]):error);
            });}];return;}
            [proxy waitForCapture:sessionID reply:^(NSError *captureError){dispatch_async(dispatch_get_main_queue(),^{
                if(captureError||self.stopped){finish(captureError?:VIMUSBError(@"This virtual Mac has stopped."));return;}
                // v1 storage compatibility keeps a device's existing BOT
                // alternate instead of entering the unreliable UAS path.
                // Preserve SuperSpeed: emulating EHCI speed alone mounted the
                // disk but produced I/O errors. USB 2 devices still use EHCI.
                unsigned int speed=[device[@"ConnectionSpeed"] unsignedIntValue];
                BOOL compatibility=[device[@"StorageInterface"] boolValue]&&speed>=kIOUSBHostConnectionSpeedSuper;
                NSString *bus=speed==kIOUSBHostConnectionSpeedHigh?@"usb.0":@"xhci.0";
                [control execute:@"device_add" arguments:@{@"driver":@"usb-redir",@"id":identifier,@"bus":bus,@"chardev":character,@"bulk-only":compatibility?@YES:@NO}
                    completion:^(id value,NSError *addError){finish(addError);}];
            });}];
        }];
    });}];
}
- (void)disconnectDevice:(NSString *)identifier control:(VIMQMPConnection *)control completion:(void (^)(NSError *))completion {
    NSDictionary *session=self.sessions[identifier];
    if(!session){completion(VIMUSBError(@"This USB connection has already ended."));return;}
    self.retiringCharacters[identifier]=session[@"Character"];
    [control execute:@"device_del" arguments:@{@"id":identifier} completion:^(id result,NSError *error){
        if(error){[self.retiringCharacters removeObjectForKey:identifier];completion(error);return;}
        id<VIMUSBHelperProtocol> proxy=[self.connection remoteObjectProxyWithErrorHandler:^(NSError *failure){}];
        [proxy endSession:session[@"RootID"] reply:^{}];
        [self.sessions removeObjectForKey:identifier];unlink([session[@"Path"] fileSystemRepresentation]);
        completion(nil);
    }];
}
- (void)deviceWasDeleted:(NSString *)identifier control:(VIMQMPConnection *)control {
    NSString *character=self.retiringCharacters[identifier];if(!character)return;
    [self.retiringCharacters removeObjectForKey:identifier];
    [control execute:@"chardev-remove" arguments:@{@"id":character} completion:^(id ignored,NSError *error){}];
}
- (void)stop {
    self.stopped=YES;self.prepared=NO;
    [self.connection invalidate];self.connection=nil;
    for(NSDictionary *session in self.sessions.allValues)unlink([session[@"Path"] fileSystemRepresentation]);
    [self.sessions removeAllObjects];
}
- (void)dealloc {[_connection invalidate];}
@end
