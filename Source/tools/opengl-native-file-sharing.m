// Native SMB compatibility: Lion negotiation integrity and native SMB2.1.
// One cold setup/update-recovery operation; no listener or ongoing polling.
#import <Foundation/Foundation.h>
#import <SystemConfiguration/SystemConfiguration.h>
#import <DiskArbitration/DiskArbitration.h>
#import "opengl-guest-machine.h"
#include "opengl-launchd-state.h"
#include "opengl-state-path.h"
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
static NSString *const Tools=@"/Library/Application Support/Virtual Intel Mac/GuestTools";
static NSString *const Library=@"opengl-lion-smb-spnego.dylib";
static NSString *const Job=@"/System/Library/LaunchDaemons/com.apple.smbd.plist";
static NSString *const RecoveryJob=@"/Library/LaunchDaemons/com.virtualintelmac.lion-smb-compat.plist";
static const int PolicyVersion=2;
static NSString *Metadata(NSString *guest){
    return [[NSDictionary dictionaryWithObjectsAndKeys:@"LionSMB.plist",@"lion",@"YosemiteSMB.plist",@"yosemite",
             @"ElCapitanSMB.plist",@"elcapitan",@"SierraSMB.plist",@"sierra",nil] objectForKey:guest];
}
static BOOL File(NSString *path){struct stat s;return !lstat([path fileSystemRepresentation],&s)&&S_ISREG(s.st_mode)&&!s.st_uid&&!(s.st_mode&0022);}
static BOOL Save(NSDictionary *value,NSString *path,mode_t mode){
    return [value writeToFile:path atomically:YES]&&!chown([path fileSystemRepresentation],0,0)&&!chmod([path fileSystemRepresentation],mode);
}
static int Run(NSString *program,NSArray *arguments){
    NSTask *task=[[[NSTask alloc] init] autorelease];NSPipe *pipe=[NSPipe pipe];
    [task setLaunchPath:program];[task setArguments:arguments];[task setStandardInput:[NSFileHandle fileHandleWithNullDevice]];
    [task setStandardOutput:pipe];[task setStandardError:pipe];
    @try{[task launch];[[pipe fileHandleForReading] readDataToEndOfFile];[task waitUntilExit];return [task terminationStatus];}
    @catch(NSException *error){return 1;}
}
static BOOL DomainReady(void){
    SCDynamicStoreRef store=SCDynamicStoreCreate(NULL,CFSTR("VirtualIntelMacLionSMB"),NULL,NULL);
    NSDictionary *value=store?(NSDictionary *)SCDynamicStoreCopyValue(store,CFSTR("com.apple.smb")):nil;
    NSString *domain=[value isKindOfClass:[NSDictionary class]]?[value objectForKey:@"NetBIOSName"]:nil;
    BOOL ready=[domain isKindOfClass:[NSString class]]&&[domain length];[value release];if(store)CFRelease(store);return ready;
}
static BOOL Listening(void){
    int fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return NO;
    struct sockaddr_in address;memset(&address,0,sizeof(address));address.sin_len=sizeof(address);
    address.sin_family=AF_INET;address.sin_port=htons(445);address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    // A single local connection checks the actual launchd listener, not just
    // an enabled plist. No credentials, share operations or remote traffic.
    int result=connect(fd,(struct sockaddr *)&address,sizeof(address));close(fd);return result==0;
}
static int StopNativeJob(void){
    NSDictionary *job=(NSDictionary *)SMJobCopyDictionary(kSMDomainSystemLaunchd,CFSTR("com.apple.smbd"));
    pid_t pid=[[job objectForKey:@"PID"] intValue];[job release];int queue=-1;
    if(pid>1){
        queue=kqueue();if(queue<0)return 75;struct kevent change;
        EV_SET(&change,pid,EVFILT_PROC,EV_ADD|EV_ONESHOT,NOTE_EXIT,0,NULL);
        if(kevent(queue,&change,1,NULL,0,NULL)<0){int error=errno;close(queue);queue=-1;if(error!=ESRCH)return 75;}
    }
    if(Run(@"/bin/launchctl",[NSArray arrayWithObjects:@"unload",Job,nil])){if(queue>=0)close(queue);return 1;}
    // launchctl unload returns before an in-flight smbd exits. Loading the
    // replacement immediately can fail to bind445, leaving an empty socket
    // array. Wait for the actual process-exit event, not an arbitrary delay.
    if(queue>=0){struct kevent event;int n;do{n=kevent(queue,NULL,0,&event,1,NULL);}while(n<0&&errno==EINTR);close(queue);if(n!=1)return 1;}
    return 0;
}
static int Failure(const char *message){fprintf(stderr,"Native File Sharing: %s.\n",message);return 1;}
static int Proof(NSDictionary *owner,NSString *volume,NSString *guest){
    NSDictionary *proof=[NSDictionary dictionaryWithObjectsAndKeys:@"installed",@"Phase",guest,@"Guest",[NSNumber numberWithInt:PolicyVersion],@"PolicyVersion",
        [owner objectForKey:@"MachineIdentifier"],@"MachineIdentifier",volume,@"VolumeUUID",nil];
    NSData *json=[NSJSONSerialization dataWithJSONObject:proof options:0 error:NULL];
    if(!json)return 1;fwrite([json bytes],1,[json length],stdout);fputc('\n',stdout);return 0;
}
static int RepairSMB2(NSDictionary *owner,NSString *volume,NSString *state,NSString *guest){
    NSString *recordName=[[Metadata(guest) stringByDeletingPathExtension] stringByAppendingString:@"Policy.plist"];
    NSString *recordPath=[state stringByAppendingPathComponent:recordName];
    NSDictionary *record=File(recordPath)?[NSDictionary dictionaryWithContentsOfFile:recordPath]:nil;
    NSString *build=[[NSDictionary dictionaryWithContentsOfFile:@"/System/Library/CoreServices/SystemVersion.plist"] objectForKey:@"ProductBuildVersion"];
    BOOL current=[[record objectForKey:@"PolicyVersion"] intValue]==PolicyVersion&&
        [[record objectForKey:@"Phase"] isEqual:@"installed"]&&[[record objectForKey:@"ProductBuildVersion"] isEqual:build];
    BOOL loaded=VIMOpenGLSystemJobLoaded(CFSTR("com.apple.smbd"));
    if(!current){
        NSString *path=@"/Library/Preferences/SystemConfiguration/com.apple.smb.server.plist";
        // Let Apple's publisher create its complete defaults. Never invent a
        // partial native server dictionary just to set this compatibility bit.
        if(!File(path))Run(@"/usr/libexec/smb-sync-preferences",[NSArray array]);
        if(!File(path))return 75;
        SCPreferencesRef prefs=SCPreferencesCreate(NULL,CFSTR("VirtualIntelMacSMB"),(CFStringRef)path);
        if(!prefs)return Failure("the native sharing preferences are unavailable");
        if(!SCPreferencesLock(prefs,true)){CFRelease(prefs);return 75;}
        id value=(id)SCPreferencesGetValue(prefs,CFSTR("ProtocolVersionMap"));
        int original=[value isKindOfClass:[NSNumber class]]?[value intValue]:7;
        // Keep the working native SMB2.1 transport and its authentication and
        // signing. These releases' SMB3 validate-negotiate replies fail on newer
        // clients. Do not change the host or enable a disabled SMB1 protocol.
        int protocols=original&~4;
        if(!(protocols&3)){SCPreferencesUnlock(prefs);CFRelease(prefs);return Failure("the chosen sharing protocols do not include SMB2 or SMB1");}
        NSMutableDictionary *transaction=[NSMutableDictionary dictionaryWithObjectsAndKeys:
            [NSNumber numberWithInt:PolicyVersion],@"PolicyVersion",@"prepared",@"Phase",build?:@"",@"ProductBuildVersion",
            [NSNumber numberWithInt:original],@"OriginalProtocolVersionMap",[NSNumber numberWithInt:protocols],@"ProtocolVersionMap",nil];
        BOOL saved=Save(transaction,recordPath,0600);
        CFNumberRef number=CFNumberCreate(NULL,kCFNumberIntType,&protocols);
        BOOL applied=saved&&SCPreferencesSetValue(prefs,CFSTR("ProtocolVersionMap"),number)&&
            SCPreferencesCommitChanges(prefs)&&SCPreferencesApplyChanges(prefs);
        CFRelease(number);SCPreferencesUnlock(prefs);CFRelease(prefs);
        if(!applied)return Failure("the native sharing compatibility setting could not be saved");
        if(Run(@"/usr/libexec/smb-sync-preferences",[NSArray array]))return 75;
        if(loaded){int stopped=StopNativeJob();if(stopped)return stopped;
            if(Run(@"/bin/launchctl",[NSArray arrayWithObjects:@"load",Job,nil]))return Failure("the native sharing service could not restart");}
        if(loaded&&!Listening())return 75;
        [transaction setObject:@"installed" forKey:@"Phase"];
        if(!Save(transaction,recordPath,0600))return Failure("the native sharing result could not be saved");
    }
    // Preserve later user preference changes. Recovery only reconfigures on
    // a changed OS build, not on every boot or each sharing action.
    if(loaded&&!Listening()){
        int stopped=StopNativeJob();if(stopped)return stopped;
        if(Run(@"/bin/launchctl",[NSArray arrayWithObjects:@"load",Job,nil])||!Listening())return 75;
    }
    NSString *label=[NSString stringWithFormat:@"com.virtualintelmac.%@-smb-compat",guest];
    NSString *job=[@"/Library/LaunchDaemons" stringByAppendingPathComponent:[label stringByAppendingPathExtension:@"plist"]];
    NSDictionary *recovery=[NSDictionary dictionaryWithObjectsAndKeys:label,@"Label",
        [NSArray arrayWithObjects:[Tools stringByAppendingPathComponent:@"opengl-file-sharing"],@"--repair",nil],@"ProgramArguments",
        [NSNumber numberWithBool:YES],@"RunAtLoad",@"/var/log/VirtualIntelMacFileSharing.log",@"StandardErrorPath",nil];
    if(!File(job)&&!Save(recovery,job,0644))return Failure("the native sharing recovery job could not be saved");
    return Proof(owner,volume,guest);
}
int main(int argc,char **argv){
    NSAutoreleasePool *pool=[NSAutoreleasePool new];
    BOOL install=argc==3&&!strcmp(argv[1],"--install");
    if(!install&&(argc!=2||strcmp(argv[1],"--repair")))return 64;
    char release[80]={0};size_t size=sizeof(release);
    if(geteuid()||sysctlbyname("kern.osrelease",release,&size,NULL,0)||!VIMOpenGLGuestMachine())return 77;
    NSString *guest=!strncmp(release,"11.",3)?@"lion":!strncmp(release,"14.",3)?@"yosemite":
        !strncmp(release,"15.",3)?@"elcapitan":!strncmp(release,"16.",3)?@"sierra":nil;
    if(!guest)return 77;
    umask(0077);NSString *state=VIM_OPENGL_STATE_PATH;
    NSString *ownerPath=[state stringByAppendingPathComponent:@"Ownership.plist"];
    NSDictionary *owner=File(ownerPath)?[NSDictionary dictionaryWithContentsOfFile:ownerPath]:nil;
    if(![[owner objectForKey:@"Guest"] isEqual:guest]||![owner objectForKey:@"MachineIdentifier"]||![owner objectForKey:@"VolumeUUID"])return 77;
    DASessionRef session=DASessionCreate(NULL);DADiskRef disk=session?DADiskCreateFromVolumePath(NULL,session,(CFURLRef)[NSURL fileURLWithPath:@"/"]):NULL;
    NSDictionary *info=disk?(NSDictionary *)DADiskCopyDescription(disk):nil;
    CFUUIDRef uuid=(CFUUIDRef)[info objectForKey:(NSString *)kDADiskDescriptionVolumeUUIDKey];
    NSString *volume=uuid?[(NSString *)CFUUIDCreateString(NULL,uuid) autorelease]:nil;
    [info release];if(disk)CFRelease(disk);if(session)CFRelease(session);
    if(!volume)return 75;if(![volume isEqual:[owner objectForKey:@"VolumeUUID"]])return 77;
    if(install){
        NSString *from=[NSString stringWithUTF8String:argv[2]];
        struct stat s;if(![[from stringByDeletingLastPathComponent] isEqual:@"/private/var/tmp"]||
           ![[from lastPathComponent] hasPrefix:@"VirtualIntelMac."]||lstat([from fileSystemRepresentation],&s)||!S_ISDIR(s.st_mode)||s.st_uid||(s.st_mode&0077))return 77;
        NSArray *files=[guest isEqual:@"lion"]?[NSArray arrayWithObjects:Library,@"opengl-file-sharing",@"LionSMB.plist",nil]:
            [NSArray arrayWithObjects:@"opengl-file-sharing",Metadata(guest),nil];
        for(NSString *name in files){
            NSString *source=[from stringByAppendingPathComponent:name];if(!File(source))return 77;
            if(Run(@"/usr/bin/install",[NSArray arrayWithObjects:@"-m",[name hasSuffix:@".plist"]?@"0644":@"0755",@"-o",@"root",@"-g",@"wheel",source,[Tools stringByAppendingPathComponent:name],nil]))return Failure("the compiled support could not be installed");
        }
    }
    if(![guest isEqual:@"lion"]){int result=RepairSMB2(owner,volume,state,guest);[pool drain];return result;}
    NSString *library=[Tools stringByAppendingPathComponent:Library];
    if(!File(library))return 66;
    NSMutableDictionary *native=File(Job)?[[[NSDictionary dictionaryWithContentsOfFile:Job] mutableCopy] autorelease]:nil;
    NSArray *arguments=[native objectForKey:@"ProgramArguments"];
    if(![[native objectForKey:@"Label"] isEqual:@"com.apple.smbd"]||![arguments count]||![[arguments objectAtIndex:0] isEqual:@"/usr/sbin/smbd"])return Failure("the native SMB service is unavailable");
    NSMutableDictionary *environment=[[[native objectForKey:@"EnvironmentVariables"] mutableCopy] autorelease];
    if(!environment)environment=[NSMutableDictionary dictionary];
    NSString *existing=[environment objectForKey:@"DYLD_INSERT_LIBRARIES"];
    NSString *policy=[NSString stringWithFormat:@"%d",PolicyVersion];
    if(existing&&![existing isEqual:library])return Failure("the native SMB service has a separate library override");
    BOOL loaded=VIMOpenGLSystemJobLoaded(CFSTR("com.apple.smbd"));
    if(![existing isEqual:library]||![[environment objectForKey:@"VIM_LION_SMB_POLICY"] isEqual:policy]){
        // Preserve the unmodified native job, not an edited executable. A
        // system update may restore that job; this cold helper reapplies it.
        if(!existing&&!Save(native,[state stringByAppendingPathComponent:@"OriginalLionSMBJob.plist"],0600))return Failure("the original native job could not be saved");
        if(loaded){int stopped=StopNativeJob();if(stopped)return stopped;}
        NSDictionary *original=[[native copy] autorelease];
        [environment setObject:library forKey:@"DYLD_INSERT_LIBRARIES"];[environment setObject:policy forKey:@"VIM_LION_SMB_POLICY"];
        [native setObject:environment forKey:@"EnvironmentVariables"];
        if(!Save(native,Job,0644)){
            Save(original,Job,0644);if(loaded)Run(@"/bin/launchctl",[NSArray arrayWithObjects:@"load",Job,nil]);
            return Failure("the native SMB job could not be updated");
        }
        if(loaded&&Run(@"/bin/launchctl",[NSArray arrayWithObjects:@"load",Job,nil]))return Failure("the native SMB service could not restart");
    }
    // A manually loaded smbd is not enough: Lion's digest-service reads the
    // native publisher's dynamic NetBIOS domain, not the static SMB plist.
    if(loaded&&!DomainReady()){
        Run(@"/usr/libexec/smb-sync-preferences",[NSArray arrayWithObject:@"--linger=false"]);
        if(!DomainReady())return 75;
    }
    if(loaded&&!Listening()){
        // Lion's native publisher may overlap the first job replacement.
        // launchd can report a loaded job despite failing to bind its socket.
        // Re-submit that actual failed listener once; never report success
        // from a job dictionary alone or spin in a background retry loop.
        int stopped=StopNativeJob();if(stopped)return stopped;
        if(Run(@"/bin/launchctl",[NSArray arrayWithObjects:@"load",Job,nil]))return Failure("the native SMB listener could not restart");
        if(!Listening())return 75;
    }
    NSDictionary *recovery=[NSDictionary dictionaryWithObjectsAndKeys:@"com.virtualintelmac.lion-smb-compat",@"Label",
        [NSArray arrayWithObjects:[Tools stringByAppendingPathComponent:@"opengl-file-sharing"],@"--repair",nil],@"ProgramArguments",
        [NSNumber numberWithBool:YES],@"RunAtLoad",@"/var/log/VirtualIntelMacFileSharing.log",@"StandardErrorPath",nil];
    if(!File(RecoveryJob)&&!Save(recovery,RecoveryJob,0644))return Failure("the cold update-recovery job could not be saved");
    int result=Proof(owner,volume,guest);[pool drain];return result;
}
