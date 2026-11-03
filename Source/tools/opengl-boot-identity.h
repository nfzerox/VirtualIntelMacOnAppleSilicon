// Mavericks has kern.bootsessionuuid; Mountain Lion does not. For older
// kernels use a root-published control-session nonce, bound to the live agent's
// kernel-reported PID/birth time. Never derive it from the adjustable wall
// clock or mistake a persistent machine UUID for a new boot. An agent restart
// conservatively starts a new control epoch; real desktop/pixel gates remain
// mandatory independently of this restart/transaction fence.
#import <Foundation/Foundation.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1050
#include <libproc.h>
#endif
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
// Tiger has no libproc. Read its genuine SDK kinfo_proc through KERN_PROC_PID;
// only this normalized private record is shared with the identity code below.
typedef struct { uint32_t pbi_pid,pbi_uid; struct timeval pbi_start; } VIMOpenGLProcessInfo;
#elif __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
// Darwin9 runs a 32-bit kernel even with x64 clients. Its PROC_PIDTBSDINFO
// copies sizeof(kernel struct proc_bsdinfo) unchanged: XNU1228.15.4,
// bsd/kern/proc_info.c proc_pidbsdinfo, bsd/sys/proc_info.h and param.h.
// The x64 10.5 SDK instead embeds a 64-bit timeval and asks for 144 bytes;
// the native kernel returns 128. Spell the actual fixed-width wire record,
// not a guessed offset into that incompatible SDK structure.
typedef struct {
    uint32_t pbi_flags,pbi_status,pbi_xstatus,pbi_pid,pbi_ppid;
    uint32_t pbi_uid,pbi_gid,pbi_ruid,pbi_rgid,pbi_svuid,pbi_svgid;
    char pbi_comm[17],pbi_name[33]; // MAXCOMLEN=16 in the actual kernel source
    uint32_t pbi_nfiles,pbi_pgid,pbi_pjobc,e_tdev,e_tpgid;
    struct {int32_t tv_sec,tv_usec;} pbi_start;
    int32_t pbi_nice;
} VIMOpenGLProcessInfo;
_Static_assert(sizeof(VIMOpenGLProcessInfo)==128,"Darwin9 process-info wire size");
#else
typedef struct proc_bsdinfo VIMOpenGLProcessInfo;
#endif
static inline BOOL VIMOpenGLNeedsControlIdentity(void){
    char release[80]={0};size_t size=sizeof(release);
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
    int mib[]={CTL_KERN,KERN_OSRELEASE};
    if(sysctl(mib,2,release,&size,NULL,0)||size>=sizeof(release))return NO;
#else
    if(sysctlbyname("kern.osrelease",release,&size,NULL,0)||size>=sizeof(release))return NO;
#endif
    long major=strtol(release,NULL,10);return major==8||major==9||major==10||major==11||major==12;
}
static inline BOOL VIMOpenGLValidBootUUID(NSString *value){
    if(![value isKindOfClass:[NSString class]])return NO;
    CFUUIDRef uuid=CFUUIDCreateFromString(NULL,(CFStringRef)value);
    if(uuid)CFRelease(uuid);return uuid!=NULL;
}
static inline NSString *VIMOpenGLNativeBootUUID(void){
    char value[80]={0};size_t size=sizeof(value);
    if(sysctlbyname("kern.bootsessionuuid",value,&size,NULL,0)||size>=sizeof(value))return nil;
    NSString *uuid=[NSString stringWithUTF8String:value];return VIMOpenGLValidBootUUID(uuid)?uuid:nil;
}
static inline BOOL VIMOpenGLBootRecordDirectory(BOOL create){
    const char *path="/var/run/VirtualIntelMacOpenGL";struct stat st;
    if(lstat(path,&st)){
        if(!create||errno!=ENOENT||mkdir(path,0700))return NO;
        if(lstat(path,&st))return NO;
    }
    return S_ISDIR(st.st_mode)&&st.st_uid==0&&!(st.st_mode&0077);
}
static inline BOOL VIMOpenGLReadProcessBirth(pid_t pid,VIMOpenGLProcessInfo *info){
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
    struct kinfo_proc native;size_t size=sizeof(native);int mib[]={CTL_KERN,KERN_PROC,KERN_PROC_PID,pid};
    if(pid<=0||sysctl(mib,4,&native,&size,NULL,0)||size!=sizeof(native)||native.kp_proc.p_pid!=pid)return NO;
    info->pbi_pid=native.kp_proc.p_pid;info->pbi_uid=native.kp_eproc.e_ucred.cr_uid;
    info->pbi_start=native.kp_proc.p_starttime;return YES;
#else
    return pid>0&&proc_pidinfo(pid,PROC_PIDTBSDINFO,0,info,sizeof(*info))==(int)sizeof(*info);
#endif
}
static inline NSString *VIMOpenGLProcessExecutable(pid_t pid){
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
    // KERN_PROCARGS2 starts with argc followed by the native executable path.
    // Read at most the kernel's bounded argmax; never publish argv/environment.
    int maximum=0;size_t length=sizeof(maximum);int maximumMIB[]={CTL_KERN,KERN_ARGMAX};
    if(sysctl(maximumMIB,2,&maximum,&length,NULL,0)||length!=sizeof(maximum)||maximum<PATH_MAX||maximum>1024*1024)return nil;
    char *buffer=malloc((size_t)maximum);if(!buffer)return nil;
    int mib[]={CTL_KERN,KERN_PROCARGS2,pid};length=(size_t)maximum;
    BOOL valid=!sysctl(mib,3,buffer,&length,NULL,0)&&length>sizeof(int);
    int argc=0;if(valid)memcpy(&argc,buffer,sizeof(argc));
    const char *end=valid?memchr(buffer+sizeof(int),0,length-sizeof(int)):NULL;
    NSString *path=argc>0&&end?[NSString stringWithUTF8String:buffer+sizeof(int)]:nil;
    free(buffer);return path;
#else
    char path[PROC_PIDPATHINFO_MAXSIZE]={0};
    if(proc_pidpath(pid,path,sizeof(path))<=0)return nil;
    return [NSString stringWithUTF8String:path];
#endif
}
static inline BOOL VIMOpenGLAgentBirth(pid_t pid,VIMOpenGLProcessInfo *info){
    return pid>1&&VIMOpenGLReadProcessBirth(pid,info)&&info->pbi_uid==0&&
        [[VIMOpenGLProcessExecutable(pid) lastPathComponent] isEqual:@"opengl-guest-agent"];
}
static inline uint64_t VIMOpenGLAgentStartSeconds(const VIMOpenGLProcessInfo *info){
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
    return info->pbi_start.tv_sec;
#else
    return info->pbi_start_tvsec;
#endif
}
static inline uint64_t VIMOpenGLAgentStartMicroseconds(const VIMOpenGLProcessInfo *info){
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
    return info->pbi_start.tv_usec;
#else
    return info->pbi_start_tvusec;
#endif
}
static inline BOOL VIMOpenGLBeginControlSessionWithIdleSleepGuard(BOOL registered){
    if(geteuid()!=0)return NO;
    if(VIMOpenGLNativeBootUUID())return YES; // No change to Mavericks' native identity.
    if(!VIMOpenGLNeedsControlIdentity())return NO;
    if(!VIMOpenGLBootRecordDirectory(YES))return NO;
    const char *path="/var/run/VirtualIntelMacOpenGL/ControlSession.plist";struct stat st;
    if(!lstat(path,&st)){
        if(!S_ISREG(st.st_mode)||st.st_uid!=0||st.st_nlink!=1||(st.st_mode&0077))return NO;
    }else if(errno!=ENOENT)return NO;
    VIMOpenGLProcessInfo process;
    if(!VIMOpenGLAgentBirth(getpid(),&process))return NO;
    CFUUIDRef uuid=CFUUIDCreate(NULL);if(!uuid)return NO;
    NSString *value=[(NSString *)CFUUIDCreateString(NULL,uuid) autorelease];CFRelease(uuid);
    NSDictionary *record=[NSDictionary dictionaryWithObjectsAndKeys:value,@"UUID",
        [NSNumber numberWithInt:getpid()],@"PID",
        [NSNumber numberWithBool:registered],@"IdleSleepGuardRegistered",
        [NSNumber numberWithUnsignedLongLong:VIMOpenGLAgentStartSeconds(&process)],@"StartSeconds",
        [NSNumber numberWithUnsignedLongLong:VIMOpenGLAgentStartMicroseconds(&process)],@"StartMicroseconds",nil];
    NSData *data=[NSPropertyListSerialization dataWithPropertyList:record format:NSPropertyListXMLFormat_v1_0 options:0 error:NULL];
    return data&&[data writeToFile:[NSString stringWithUTF8String:path] options:NSDataWritingAtomic error:NULL]&&!chmod(path,0600);
}
static inline BOOL VIMOpenGLBeginControlSession(void){return VIMOpenGLBeginControlSessionWithIdleSleepGuard(NO);}
static inline NSString *VIMOpenGLBootSessionUUID(NSString **source){
    NSString *native=VIMOpenGLNativeBootUUID();
    if(native){if(source)*source=@"kernel";return native;}
    if(geteuid()!=0||!VIMOpenGLNeedsControlIdentity()||!VIMOpenGLBootRecordDirectory(NO))return nil;
    NSString *path=@"/var/run/VirtualIntelMacOpenGL/ControlSession.plist";struct stat st;
    if(lstat([path fileSystemRepresentation],&st)||!S_ISREG(st.st_mode)||st.st_uid!=0||
       st.st_nlink!=1||(st.st_mode&0077)||st.st_size>4096)return nil;
    NSDictionary *record=[NSDictionary dictionaryWithContentsOfFile:path];
    NSString *uuid=[record objectForKey:@"UUID"];VIMOpenGLProcessInfo process;
    if(!VIMOpenGLValidBootUUID(uuid)||!VIMOpenGLAgentBirth([[record objectForKey:@"PID"] intValue],&process)||
       VIMOpenGLAgentStartSeconds(&process)!=[[record objectForKey:@"StartSeconds"] unsignedLongLongValue]||
       VIMOpenGLAgentStartMicroseconds(&process)!=[[record objectForKey:@"StartMicroseconds"] unsignedLongLongValue])return nil;
    if(source)*source=@"guest-agent-start";return uuid;
}
static inline BOOL VIMOpenGLControlIdleSleepGuard(NSString *boot){
    // Tie this non-secret capability to the validated *live* agent epoch,
    // not a persistent preference, stale daemon PID or copied VM state.
    if(!boot||![boot isEqual:VIMOpenGLBootSessionUUID(NULL)])return NO;
    NSDictionary *record=[NSDictionary dictionaryWithContentsOfFile:@"/var/run/VirtualIntelMacOpenGL/ControlSession.plist"];
    return [[record objectForKey:@"UUID"] isEqual:boot]&&
        [[record objectForKey:@"IdleSleepGuardRegistered"] isEqual:[NSNumber numberWithBool:YES]];
}
