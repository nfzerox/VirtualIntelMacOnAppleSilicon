#pragma once
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
#import <Foundation/Foundation.h>
#include "opengl-guest-machine.h"
#include "opengl-launchd-state.h"
#include <dlfcn.h>
#include <sys/utsname.h>
// Actual Tiger Admin ObjC1 metadata, exercised on the installed8R4088 OS.
// No private ivar offsets, authorization bypass or donated SFL/framework.
@protocol VIMTigerNativeUser
+ (id)findUserByName:(NSString *)name searchParent:(BOOL)search;
- (BOOL)isLocal;
- (int)userID;
- (BOOL)verifyPassword:(NSString *)password;
- (BOOL)isSMBCompatible;
- (BOOL)setSMBCompatible:(BOOL)enabled password:(NSString *)password;
@end
@protocol VIMTigerNativeServices
+ (id)sharedInternetServices;
- (BOOL)sshServerEnabled;
- (BOOL)smbServerEnabled;
- (void)setSSHServerEnabled:(BOOL)enabled;
- (void)setSMBServerEnabled:(BOOL)enabled;
@end
@protocol VIMTigerNativeAuthenticator
+ (id)sharedAuthenticator;
- (BOOL)authenticateUsingAuthorizationSync:(id)authorization;
- (BOOL)isAuthenticated;
@end
@protocol VIMTigerNativeAuthorization
+ (id)authorization;
@end
static inline BOOL VIMTigerNativeServiceContext(void) {
    struct utsname kernel;
    return geteuid()==0&&!uname(&kernel)&&!strncmp(kernel.release,"8.",2)&&VIMOpenGLGuestMachine();
}
static inline BOOL VIMTigerLoadAdmin(void) {
    // Keep the image alive for its registered ObjC classes until process exit.
    static void *image;
    if(!image)image=dlopen("/System/Library/PrivateFrameworks/Admin.framework/Versions/A/Admin",RTLD_NOW|RTLD_LOCAL);
    return image!=NULL;
}
static inline id<VIMTigerNativeUser> VIMTigerNativeUser(NSString *name,uid_t uid) {
    if(!VIMTigerNativeServiceContext()||!name||uid<501||!VIMTigerLoadAdmin())return nil;
    Class type=NSClassFromString(@"User");
    if(!type||![(id)type respondsToSelector:@selector(findUserByName:searchParent:)])return nil;
    id<VIMTigerNativeUser> user=[(id)type findUserByName:name searchParent:NO];
    if(!user||![(id)user respondsToSelector:@selector(isLocal)]||
       ![(id)user respondsToSelector:@selector(userID)]||![user isLocal]||[user userID]!=(int)uid)return nil;
    return user;
}
static inline BOOL VIMTigerSMBUserReady(NSString *name,uid_t uid) {
    id<VIMTigerNativeUser> user=VIMTigerNativeUser(name,uid);
    return user&&[(id)user respondsToSelector:@selector(isSMBCompatible)]&&[user isSMBCompatible];
}
// Caller must already hold the exact account transaction lock and verify its
// root-volume, UID/GUID and original request. Reuse the supplied *current*
// password; never set a new password, manufacture hashes or touch other users.
static inline BOOL VIMTigerPrepareSMBUser(NSString *name,uid_t uid,NSString *password) {
    id<VIMTigerNativeUser> user=VIMTigerNativeUser(name,uid);
    if(!user||!password||![(id)user respondsToSelector:@selector(verifyPassword:)]||
       ![(id)user respondsToSelector:@selector(setSMBCompatible:password:)]||
       ![(id)user respondsToSelector:@selector(isSMBCompatible)]||![user verifyPassword:password])return NO;
    if(![user isSMBCompatible]&&![user setSMBCompatible:YES password:password])return NO;
    user=VIMTigerNativeUser(name,uid);
    return user&&[user isSMBCompatible]&&[user verifyPassword:password];
}
static inline id<VIMTigerNativeServices> VIMTigerNativeInternetServices(void) {
    if(!VIMTigerNativeServiceContext()||!VIMTigerLoadAdmin())return nil;
    Class type=NSClassFromString(@"InternetServices");
    if(!type||![(id)type respondsToSelector:@selector(sharedInternetServices)])return nil;
    id<VIMTigerNativeServices> value=[(id)type sharedInternetServices];
    if(!value||![(id)value respondsToSelector:@selector(sshServerEnabled)]||
       ![(id)value respondsToSelector:@selector(smbServerEnabled)]||
       ![(id)value respondsToSelector:@selector(setSSHServerEnabled:)]||
       ![(id)value respondsToSelector:@selector(setSMBServerEnabled:)])return nil;
    return value;
}
static inline BOOL VIMTigerAuthorizeNativeServices(void) {
    // euid0 alone does not establish Admin's privileged-tool connection.
    // These frameworks are installed-system-only; never link them into the
    // native DVD agent, which must also work before they have been installed.
    static void *security;
    if(!security)security=dlopen("/System/Library/Frameworks/SecurityFoundation.framework/Versions/A/SecurityFoundation",RTLD_NOW|RTLD_LOCAL);
    Class authType=NSClassFromString(@"Authenticator"),authorizationType=NSClassFromString(@"SFAuthorization");
    if(!security||!authType||!authorizationType||
       ![(id)authType respondsToSelector:@selector(sharedAuthenticator)]||
       ![(id)authorizationType respondsToSelector:@selector(authorization)])return NO;
    id<VIMTigerNativeAuthenticator> auth=[(id)authType sharedAuthenticator];
    id authorization=[(id)authorizationType authorization];
    return auth&&[(id)auth respondsToSelector:@selector(authenticateUsingAuthorizationSync:)]&&
        [(id)auth respondsToSelector:@selector(isAuthenticated)]&&authorization&&
        [auth authenticateUsingAuthorizationSync:authorization]&&[auth isAuthenticated];
}
static inline int VIMTigerEnableNativeServices(BOOL fileSharing,BOOL remoteLogin) {
    id<VIMTigerNativeServices> services=VIMTigerNativeInternetServices();if(!services)return 1;
    BOOL needSMB=fileSharing&&![services smbServerEnabled],needSSH=remoteLogin&&![services sshServerEnabled];
    if((needSMB||needSSH)&&!VIMTigerAuthorizeNativeServices())return 1;
    if(needSMB)[services setSMBServerEnabled:YES];
    if(needSSH)[services setSSHServerEnabled:YES];
    // A persisted policy is not an active server. Native launchd publication
    // can be pending; return75 to the existing cold setup cadence, never spin.
    if(fileSharing&&(![services smbServerEnabled]||!VIMOpenGLSystemJobLoaded(CFSTR("org.samba.smbd"))))return 75;
    if(remoteLogin&&(![services sshServerEnabled]||!VIMOpenGLSystemJobLoaded(CFSTR("com.openssh.sshd"))))return 75;
    return 0;
}
#endif
