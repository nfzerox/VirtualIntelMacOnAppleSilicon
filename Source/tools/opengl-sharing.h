#pragma once
// Mavericks SharingPref -> ADMInternetServices -> NetFSServer, traced in the
// actual 13F34 binaries. A launchd load alone omits EnabledServices = (disk),
// allowing Apple's smb-sync-preferences to turn the service off again.
#import <Foundation/Foundation.h>
#import <CoreFoundation/CoreFoundation.h>
#import <CoreServices/CoreServices.h>
#import <Security/Authorization.h>
#include "opengl-launchd-state.h"
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <dlfcn.h>
#include <errno.h>
#include <unistd.h>
static inline BOOL VIMOpenGLSnowSharing(void){
    char release[80]={0};size_t size=sizeof(release);
    return !sysctlbyname("kern.osrelease",release,&size,NULL,0)&&size<sizeof(release)&&!strncmp(release,"10.",3);
}
static inline BOOL VIMOpenGLLeopardSharing(void){
    char release[80]={0};size_t size=sizeof(release);
    return !sysctlbyname("kern.osrelease",release,&size,NULL,0)&&size<sizeof(release)&&!strncmp(release,"9.",2);
}
static inline CFStringRef VIMOpenGLFileSharingJobLabel(void){
    return (VIMOpenGLLeopardSharing()||VIMOpenGLSnowSharing())?CFSTR("org.samba.smbd"):CFSTR("com.apple.smbd");
}
// smb-sync-preferences creates this asynchronously on a newly installed OS.
// NetFSServer's _update_services_in_plist returns without writing anything if
// its initial _read_plist fails. Do not manufacture Apple's defaults or treat
// that first-boot ordering as an invalid account. 75 means retry cold setup.
static inline int VIMOpenGLFileSharingReadiness(void){
    const char *path="/Library/Preferences/SystemConfiguration/com.apple.smb.server.plist";
    struct stat st;
    if(lstat(path,&st)){
        if(errno==ENOENT)return 75;
        fprintf(stderr,"File sharing preferences: stat failed (%d).\n",errno);return 1;
    }
    if(!S_ISREG(st.st_mode)||st.st_uid!=0||(st.st_mode&0022)||
       ![NSDictionary dictionaryWithContentsOfFile:[NSString stringWithUTF8String:path]]){
        fprintf(stderr,"File sharing preferences are not a valid root-owned system dictionary.\n");return 1;
    }
    return 0;
}
static inline BOOL VIMOpenGLLionSharing(void){
    char release[80]={0};size_t size=sizeof(release);
    return !sysctlbyname("kern.osrelease",release,&size,NULL,0)&&size<sizeof(release)&&!strncmp(release,"11.",3);
}
static inline BOOL VIMOpenGLPersistentJobSharing(void){
    char release[80]={0};size_t size=sizeof(release);
    if(sysctlbyname("kern.osrelease",release,&size,NULL,0)||size>=sizeof(release))return NO;
    int kernel=atoi(release);return kernel>=14&&kernel<=20;
}
static inline BOOL VIMOpenGLLionSharingEnabled(void){
    // Native Lion SharingPref uses this persisted SFL policy directly; the
    // later NetFSServer framework and sharing command do not exist there.
    const CFStringRef *listName=dlsym(RTLD_DEFAULT,"kLSSharedFileListSharePoints");
    const CFStringRef *property=dlsym(RTLD_DEFAULT,"kLSSharedFileListSMBSharingEnabled");
    if(!listName||!property||!*listName||!*property)return NO;
    LSSharedFileListRef list=LSSharedFileListCreate(NULL,*listName,NULL);if(!list)return NO;
    CFTypeRef value=LSSharedFileListCopyProperty(list,*property);CFRelease(list);
    BOOL enabled=value&&CFGetTypeID(value)==CFBooleanGetTypeID()&&CFBooleanGetValue((CFBooleanRef)value);
    if(value)CFRelease(value);return enabled;
}
static inline BOOL VIMOpenGLFileSharingConfigured(void){
    if(VIMOpenGLLeopardSharing()||VIMOpenGLSnowSharing()||VIMOpenGLLionSharing())return VIMOpenGLLionSharingEnabled();
    if(VIMOpenGLPersistentJobSharing()){
        // Actual14F27 and15G31 NetFSServer no longer write Mavericks' EnabledServices
        // array. Its enable/isenabled functions use SMJobSetEnabled and
        // SMJobIsEnabled, including launchd's persistent override policy.
        void *library=dlopen("/System/Library/PrivateFrameworks/NetFSServer.framework/NetFSServer",RTLD_LAZY|RTLD_LOCAL);
        int (*enabled)(const char *,int *)=library?dlsym(library,"nsc_server_isenabled"):NULL;
        int on=0;BOOL result=enabled&&!enabled("smb",&on)&&on;
        if(library)dlclose(library);return result;
    }
    id services=[[NSDictionary dictionaryWithContentsOfFile:@"/Library/Preferences/SystemConfiguration/com.apple.smb.server.plist"] objectForKey:@"EnabledServices"];
    return [services isKindOfClass:[NSArray class]]&&[services containsObject:@"disk"];
}
static inline BOOL VIMOpenGLClassicSharing(void){
    char release[80]={0};size_t size=sizeof(release);
    return !sysctlbyname("kern.osrelease",release,&size,NULL,0)&&size<sizeof(release)&&
        (!strncmp(release,"9.",2)||!strncmp(release,"10.",3)||!strncmp(release,"11.",3)||!strncmp(release,"12.",3));
}
static inline int VIMOpenGLPublishClassicSharing(void){
    // Actual 10.5/10.6/10.7/10.8 SharingPref uses the SharePoints list's
    // SMBSharingEnabled property. Unlike 10.9, NetFSServer only enables the job
    // and never publishes EnabledServices=(disk); smb-sync-preferences later
    // removes that incomplete service. Let Apple's list service own the write.
    const CFStringRef *listName=dlsym(RTLD_DEFAULT,"kLSSharedFileListSharePoints");
    const CFStringRef *property=dlsym(RTLD_DEFAULT,"kLSSharedFileListSMBSharingEnabled");
    if(!listName||!property||!*listName||!*property)return 1;
    LSSharedFileListRef list=LSSharedFileListCreate(NULL,*listName,NULL);
    if(!list)return 75;
    AuthorizationRef authorization=NULL;
    OSStatus result=AuthorizationCreate(NULL,NULL,kAuthorizationFlagDefaults,&authorization);
    if(!result)result=LSSharedFileListSetAuthorization(list,authorization);
    if(!result)result=LSSharedFileListSetProperty(list,*property,kCFBooleanTrue);
    if(authorization)AuthorizationFree(authorization,kAuthorizationFlagDefaults);
    CFRelease(list);
    if(result)fprintf(stderr,"Native file sharing policy failed (%d).\n",(int)result);
    return result?1:0;
}
static inline int VIMOpenGLConfigureFileSharing(void){
    if(geteuid()!=0)return 1;
    int ready=VIMOpenGLFileSharingReadiness();if(ready)return ready;
    if(VIMOpenGLClassicSharing()&&!VIMOpenGLFileSharingConfigured()){
        int policy=VIMOpenGLPublishClassicSharing();if(policy)return policy;
        if(!VIMOpenGLFileSharingConfigured())return 75;
    }
    if(VIMOpenGLLeopardSharing()||VIMOpenGLSnowSharing()||VIMOpenGLLionSharing()){
        // 10J3250 SharingPref-193.1 uses the same native SFL property,
        // but its real on-demand server is Samba, not Lion's Apple smbd.
        // Let the native list service publish/enable it; no hand-built smb.conf.
        BOOL loaded=VIMOpenGLSystemJobLoaded(VIMOpenGLFileSharingJobLabel());
        return loaded&&VIMOpenGLLionSharingEnabled()?0:75;
    }
    CFURLRef url=CFURLCreateWithFileSystemPath(NULL,CFSTR("/System/Library/PrivateFrameworks/NetFSServer.framework"),kCFURLPOSIXPathStyle,true);
    CFBundleRef bundle=url?CFBundleCreate(NULL,url):NULL;if(url)CFRelease(url);if(!bundle)return 1;
    int (*enable)(const char *)=(void *)CFBundleGetFunctionPointerForName(bundle,CFSTR("nsc_server_enable"));
    int (*disable)(const char *)=(void *)CFBundleGetFunctionPointerForName(bundle,CFSTR("nsc_server_disable"));
    int (*enabled)(const char *,int *)=(void *)CFBundleGetFunctionPointerForName(bundle,CFSTR("nsc_server_isenabled"));
    if(!enable||!disable||!enabled){CFRelease(bundle);fprintf(stderr,"Native file sharing API is unavailable.\n");return 1;}
    int on=0,result=enabled("smb",&on);
    // Repair only the owned, unfinished setup transaction's partially enabled
    // state. The native enable routine otherwise short-circuits on that job.
    if(!result&&on&&!VIMOpenGLFileSharingConfigured())result=disable("smb");
    if(!result)result=enable("smb");
    if(!result){on=0;result=enabled("smb",&on);}
    int outcome=result?1:((on&&VIMOpenGLFileSharingConfigured())?0:75);
    if(result)fprintf(stderr,"Native file sharing configuration failed (%d).\n",result);
    CFRelease(bundle);return outcome;
}
