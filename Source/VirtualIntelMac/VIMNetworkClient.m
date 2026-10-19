#import "VIMNetworkClient.h"
#import "../NetworkHelper/VIMNetworkProtocol.h"
#import <ServiceManagement/ServiceManagement.h>
#import <Security/Authorization.h>
#import <Security/AuthorizationTags.h>
#include <readpassphrase.h>
#include <fcntl.h>
#include <unistd.h>

static void InstallNetworkHelper(BOOL terminal,void (^completion)(NSError *)) {
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
        AuthorizationRef authorization=NULL;OSStatus status=AuthorizationCreate(NULL,kAuthorizationEmptyEnvironment,0,&authorization);
        AuthorizationItem right={kSMRightBlessPrivilegedHelper,0,NULL,0};AuthorizationRights rights={1,&right};
        char password[1024]={0};const char *user=NSUserName().UTF8String;
        AuthorizationItem items[]={{kAuthorizationEnvironmentUsername,strlen(user),(void *)user,0},{kAuthorizationEnvironmentPassword,0,password,0}};
        AuthorizationEnvironment environment={2,items};
        if(!status&&terminal&&!readpassphrase("Administrator password for networking helper: ",password,sizeof(password),RPP_ECHO_OFF|RPP_REQUIRE_TTY))status=errAuthorizationCanceled;
        items[1].valueLength=strlen(password);
        if(!status)status=AuthorizationCopyRights(authorization,&rights,terminal?&environment:kAuthorizationEmptyEnvironment,
            kAuthorizationFlagExtendRights|kAuthorizationFlagPreAuthorize|(terminal?0:kAuthorizationFlagInteractionAllowed),NULL);
        memset_s(password,sizeof(password),0,sizeof(password));CFErrorRef error=NULL;BOOL blessed=NO;
        if(!status)blessed=SMJobBless(kSMDomainSystemLaunchd,(__bridge CFStringRef)VIM_NETWORK_SERVICE,authorization,&error);
        if(authorization)AuthorizationFree(authorization,kAuthorizationFlagDestroyRights);
        NSError *failure=CFBridgingRelease(error);
        if(status||!blessed)failure=failure?:VIMNetworkError(status==errAuthorizationCanceled?@"Network setup was cancelled.":@"Approve administrator access to enable host-only and bridged networking. Your virtual Macs do not run as administrator.");
        dispatch_async(dispatch_get_main_queue(),^{completion(failure);});
    });
}
@interface VIMNetworkClient ()
@property NSXPCConnection *connection;
@property NSFileHandle *socket;
@property NSDictionary *details;
@property BOOL stopped;
@end
@implementation VIMNetworkClient
+ (void)authorizeUsingTerminalWithCompletion:(void (^)(NSError *))completion {
    if(!NSBundle.mainBundle.infoDictionary[@"VIMNetworkHelperSourceHash"]){completion(VIMNetworkError(@"Build the complete app with its networking helper before authorizing it."));return;}
    InstallNetworkHelper(YES,completion);
}
- (void)connect {
    [self.connection invalidate];
    self.connection=[[NSXPCConnection alloc] initWithMachServiceName:VIM_NETWORK_SERVICE options:NSXPCConnectionPrivileged];
    self.connection.remoteObjectInterface=[NSXPCInterface interfaceWithProtocol:@protocol(VIMNetworkHelperProtocol)];
    NSString *requirement=NSBundle.mainBundle.infoDictionary[@"SMPrivilegedExecutables"][VIM_NETWORK_SERVICE];
    if(@available(macOS 13.0,*)){if(requirement.length)[self.connection setCodeSigningRequirement:requirement];}
    [self.connection resume];
}
- (void)startMode:(NSString *)mode bridge:(NSString *)bridge completion:(void (^)(NSError *))completion {
    if(!NSBundle.mainBundle.infoDictionary[@"VIMNetworkHelperSourceHash"]){completion(VIMNetworkError(@"This app is missing its networking helper. Open the complete Virtual Intel Mac app."));return;}
    [self connect];[self check:^(BOOL ready){
        if(self.stopped){completion(VIMNetworkError(@"Network setup was cancelled."));return;}
        if(ready){[self beginMode:mode bridge:bridge completion:completion];return;}
        InstallNetworkHelper(NO,^(NSError *error){
            if(error||self.stopped){completion(error?:VIMNetworkError(@"Network setup was cancelled."));return;}
            [self connect];[self check:^(BOOL valid){
                if(!valid){completion(VIMNetworkError(@"The networking helper did not become available. Try starting this Mac again."));return;}
                [self beginMode:mode bridge:bridge completion:completion];
            }];
        });
    }];
}
- (void)check:(void (^)(BOOL))completion {
    __block BOOL answered=NO;
    void (^finish)(BOOL)=^(BOOL valid){dispatch_async(dispatch_get_main_queue(),^{if(answered)return;answered=YES;completion(valid);});};
    id<VIMNetworkHelperProtocol> proxy=[self.connection remoteObjectProxyWithErrorHandler:^(NSError *error){finish(NO);}];
    [proxy statusWithReply:^(NSDictionary *status){finish([status[@"Protocol"] isEqual:@(VIM_NETWORK_PROTOCOL)]&&[status[@"SourceHash"] isEqual:NSBundle.mainBundle.infoDictionary[@"VIMNetworkHelperSourceHash"]]);}];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,5*NSEC_PER_SEC),dispatch_get_main_queue(),^{finish(NO);});
}
- (void)beginMode:(NSString *)mode bridge:(NSString *)bridge completion:(void (^)(NSError *))completion {
    __block BOOL answered=NO;
    void (^finish)(NSError *)=^(NSError *error){if(answered)return;answered=YES;if(error)[self stop];completion(error);};
    id<VIMNetworkHelperProtocol> proxy=[self.connection remoteObjectProxyWithErrorHandler:^(NSError *error){dispatch_async(dispatch_get_main_queue(),^{finish(error);});}];
    [proxy startMode:mode bridge:bridge reply:^(NSFileHandle *socket,NSDictionary *details,NSError *error){
        int fd=socket?dup(socket.fileDescriptor):-1;
        dispatch_async(dispatch_get_main_queue(),^{
            if(answered||self.stopped){if(fd>=0)close(fd);if(!answered)finish(VIMNetworkError(@"Network setup was cancelled."));return;}
            if(error||fd<0){finish(error?:VIMNetworkError(@"The network connection could not be opened."));return;}
            fcntl(fd,F_SETFD,FD_CLOEXEC);self.socket=[[NSFileHandle alloc] initWithFileDescriptor:fd closeOnDealloc:YES];self.details=details;finish(nil);
        });
    }];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,30*NSEC_PER_SEC),dispatch_get_main_queue(),^{if(!answered)finish(VIMNetworkError(@"macOS did not finish creating this network. Try starting this Mac again."));});
}
- (void)stop {self.stopped=YES;[self.socket closeAndReturnError:NULL];self.socket=nil;[self.connection invalidate];self.connection=nil;}
- (void)dealloc {[_connection invalidate];}
@end
