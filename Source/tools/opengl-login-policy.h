// Root-only, canonical system login preference. Absolute-domain addressing is
// the native equivalent of `defaults write /Library/Preferences/...`, verified
// in the installed 13F34 system. A daemon's AnyUser bundle-ID write did not
// publish this file, despite a valid account/credential handoff.
#import <Foundation/Foundation.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
static NSString *VIMOpenGLLoginUser(void) {
    NSString *path=@"/Library/Preferences/com.apple.loginwindow.plist";struct stat st;
    if(lstat([path fileSystemRepresentation],&st)||!S_ISREG(st.st_mode)||st.st_uid!=0||
       st.st_nlink!=1||(st.st_mode&0022)||st.st_size>1024*1024)return nil;
    id name=[[NSDictionary dictionaryWithContentsOfFile:path] objectForKey:@"autoLoginUser"];
    return [name isKindOfClass:[NSString class]]?name:nil;
}
static BOOL VIMOpenGLSetLoginUserWithPendingPublication(NSString *name,BOOL allowPendingSync) {
    errno=0;
    if(geteuid()!=0)return NO;
    const char *path="/Library/Preferences/com.apple.loginwindow.plist";struct stat st;
    if(!lstat(path,&st)){
        if(!S_ISREG(st.st_mode)||st.st_uid!=0||st.st_nlink!=1||(st.st_mode&0022))return NO;
    }else if(errno!=ENOENT)return NO;
    CFStringRef domain=CFSTR("/Library/Preferences/com.apple.loginwindow");
    CFPreferencesSetValue(CFSTR("autoLoginUser"),(CFStringRef)name,domain,kCFPreferencesCurrentUser,kCFPreferencesAnyHost);
    BOOL synchronized=CFPreferencesSynchronize(domain,kCFPreferencesCurrentUser,kCFPreferencesAnyHost);
    // High Sierra's cold cfprefsd can report a failed synchronization while
    // still publishing the requested preference. Confirm the actual native
    // file instead. Only the owned setup operation may wait and retry a
    // missing publication; clearing policy or unsafe files must fail closed.
    if(!synchronized&&!allowPendingSync)return NO;
    // Lion's loginwindow authorization plugin runs as _securityagent, not
    // root. CFPreferences inherits our transaction's private umask on first
    // publication; 0600 silently hides autoLoginUser from that plugin. Match
    // Apple's public system preference (0644); the credential stays 0600.
    int descriptor=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    if(descriptor<0){if(errno==ENOENT)errno=EAGAIN;return NO;}
    BOOL published=!fstat(descriptor,&st)&&S_ISREG(st.st_mode)&&st.st_uid==0&&
        st.st_nlink==1&&!(st.st_mode&0022)&&!fchmod(descriptor,0644);
    close(descriptor);if(!published)return NO;
    NSString *actual=VIMOpenGLLoginUser();
    BOOL confirmed=name?[actual isEqual:name]:actual==nil;
    // High Sierra's first system-preference publication can outlive the
    // successful CFPreferences synchronization. Retain the owned handoff and
    // let the cold setup coordinator retry; never call this a finished login.
    if(!confirmed)errno=EAGAIN;
    return confirmed;
}
static inline BOOL VIMOpenGLSetLoginUser(NSString *name) {
    return VIMOpenGLSetLoginUserWithPendingPublication(name,NO);
}
#ifndef VIM_MODERN_ACCOUNT_SETUP
static inline BOOL VIMCanRecoverLoginHandoff(NSString *version) {
    return [version hasPrefix:@"10.11."]||[version hasPrefix:@"10.12."];
}
// El Capitan and Sierra can clear both native settings before first desktop
// login. Recover an unfinished, owned transaction once. An interrupted
// publication can resume, but may not take over someone else's login policy.
static inline BOOL VIMLoginHandoffNeedsRecovery(NSString *version,NSDictionary *ownership,
                                                NSDictionary *account,NSString *boot,uid_t consoleUID) {
    NSString *armed=[account objectForKey:@"FirstLoginPolicyBootSessionUUID"];
    if(!VIMCanRecoverLoginHandoff(version)||![[ownership objectForKey:@"Enabled"] boolValue]||
       ![[ownership objectForKey:@"AutomaticLogin"] boolValue]||consoleUID!=0||
       ![[account objectForKey:@"Phase"] isEqual:@"autologin-armed"]||
       ![[account objectForKey:@"FirstLoginPolicyPrepared"] boolValue]||[account objectForKey:@"LoginHandoffRecovered"]||
       !boot.length||!armed.length||[armed isEqual:boot])return NO;
    BOOL pending=[[account objectForKey:@"LoginHandoffRecoveryPending"] boolValue];
    struct stat st;
    if(lstat("/etc/kcpassword",&st)){
        if(errno!=ENOENT)return NO;
    }else if(!pending||!S_ISREG(st.st_mode)||st.st_uid!=0||st.st_nlink!=1||(st.st_mode&0077))return NO;
    NSString *path=@"/Library/Preferences/com.apple.loginwindow.plist";
    if(lstat([path fileSystemRepresentation],&st))return errno==ENOENT;
    if(!S_ISREG(st.st_mode)||st.st_uid!=0||st.st_nlink!=1||(st.st_mode&0022))return NO;
    NSDictionary *prefs=[NSDictionary dictionaryWithContentsOfFile:path];
    id user=[prefs objectForKey:@"autoLoginUser"],uid=[prefs objectForKey:@"autoLoginUserUID"];
    return prefs&&(!user||(pending&&[user isEqual:[account objectForKey:@"Username"]]))&&
        (!uid||(pending&&[uid isEqual:[account objectForKey:@"UID"]]));
}
#endif
