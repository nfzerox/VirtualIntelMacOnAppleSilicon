// Read-only guest installation evidence. This is NOT authorization to delete
// media or patch a volume; the host must authenticate a service and validate a
// completed patch transaction before declaring the machine ready.
#import <Foundation/Foundation.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <arpa/inet.h>
#import <SystemConfiguration/SystemConfiguration.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <libproc.h>
#import "guest-console-state.h"

static NSArray *NetworkInterfaces(void) {
    struct ifaddrs *list=NULL;NSMutableDictionary *interfaces=[NSMutableDictionary new];
    if(getifaddrs(&list))return @[];
    for(struct ifaddrs *entry=list;entry;entry=entry->ifa_next) {
        if(!entry->ifa_addr||!(entry->ifa_flags&IFF_UP)||(entry->ifa_flags&IFF_LOOPBACK))continue;
        NSString *name=@(entry->ifa_name);NSMutableDictionary *interface=interfaces[name];
        if(!interface){interface=[@{@"Name":name,@"Addresses":[NSMutableArray new]} mutableCopy];interfaces[name]=interface;}
        if(entry->ifa_addr->sa_family==AF_LINK) {
            struct sockaddr_dl *link=(struct sockaddr_dl *)entry->ifa_addr;
            if(link->sdl_alen==6){const unsigned char *mac=(const unsigned char *)LLADDR(link);interface[@"MACAddress"]=[NSString stringWithFormat:@"%02X:%02X:%02X:%02X:%02X:%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]];}
        } else if(entry->ifa_addr->sa_family==AF_INET) {
            char text[INET_ADDRSTRLEN];if(inet_ntop(AF_INET,&((struct sockaddr_in *)entry->ifa_addr)->sin_addr,text,sizeof(text)))[interface[@"Addresses"] addObject:@(text)];
        }
    }freeifaddrs(list);
    NSMutableArray *result=[NSMutableArray new];for(NSString *name in [interfaces.allKeys sortedArrayUsingSelector:@selector(compare:)])[result addObject:interfaces[name]];
    return result;
}
static BOOL DesktopRunning(void) {
    // Do not use Finder.app's existence as evidence of a logged-in desktop.
    pid_t pids[8192];int bytes=proc_listpids(PROC_ALL_PIDS,0,pids,sizeof(pids));
    BOOL finder=NO,setup=NO;
    for(int i=0;i<bytes/(int)sizeof(pid_t);i++) {
        char path[PROC_PIDPATHINFO_MAXSIZE]={0};
        if(proc_pidpath(pids[i],path,sizeof(path))<=0)continue;
        if(strstr(path,"/Finder.app/Contents/MacOS/Finder"))finder=YES;
        if(strstr(path,"/Setup Assistant.app/Contents/MacOS/"))setup=YES;
    }
    return finder&&!setup;
}

static NSString *SysctlString(const char *key) {
    size_t size=0;
    if(sysctlbyname(key,NULL,&size,NULL,0)||!size||size>65536)return @"";
    NSMutableData *data=[NSMutableData dataWithLength:size];
    if(sysctlbyname(key,data.mutableBytes,&size,NULL,0))return @"";
    return [[NSString alloc] initWithBytes:data.bytes length:strnlen(data.bytes,size) encoding:NSUTF8StringEncoding]?:@"";
}
int main(int argc,const char *argv[]) {@autoreleasepool {
    if(argc!=1 && !(argc==3 && !strcmp(argv[1],"--root"))) {
        fprintf(stderr,"usage: guest-status [--root ABSOLUTE_MOUNT_PATH]\n");return 64;
    }
    NSString *root=argc==3?@(argv[2]):@"/";
    if(!root.isAbsolutePath){fprintf(stderr,"root must be absolute\n");return 64;}
    root=root.stringByStandardizingPath.stringByResolvingSymlinksInPath;
    BOOL live=[root isEqual:@"/"];
    NSFileManager *fm=NSFileManager.defaultManager;
    NSString *(^path)(NSString *)=^NSString *(NSString *relative){return [root stringByAppendingPathComponent:relative];};
    NSError *error=nil;
    NSData *data=[NSData dataWithContentsOfFile:path(@"System/Library/CoreServices/SystemVersion.plist") options:0 error:&error];
    id version=data?[NSPropertyListSerialization propertyListWithData:data options:NSPropertyListImmutable format:NULL error:&error]:nil;
    if(![version isKindOfClass:NSDictionary.class] || ![version[@"ProductVersion"] isKindOfClass:NSString.class] ||
       ![version[@"ProductBuildVersion"] isKindOfClass:NSString.class]) {
        fprintf(stderr,"Cannot read valid target SystemVersion.plist\n");return 65;
    }
    NSString *release=version[@"ProductVersion"],*build=version[@"ProductBuildVersion"];
    NSArray *components=[release componentsSeparatedByString:@"."];
    BOOL numeric=components.count>=2;
    for(NSString *part in components)if(!part.length||[part rangeOfCharacterFromSet:NSCharacterSet.decimalDigitCharacterSet.invertedSet].location!=NSNotFound)numeric=NO;
    NSString *guest=@"unsupported";
    if(numeric) {
        if([components[0] isEqual:@"10"]&&[components[1] isEqual:@"14"])guest=@"mojave";
        else if([components[0] isEqual:@"10"]&&[components[1] isEqual:@"15"])guest=@"catalina";
        else if([components[0] isEqual:@"11"])guest=@"bigsur";
    }
    NSMutableArray *pending=[NSMutableArray array];
    // .OSInstallerMessages is an installer analytics/messages plist, not a
    // pending-install checkpoint. Security updates can leave it on a normal
    // installed root after removing macOS Install Data. Blocking on that file
    // deadlocks headless graphics repair when WindowServer cannot start.
    for(NSString *marker in @[@"macOS Install Data",@"OS X Install Data"])
        if([fm fileExistsAtPath:path(marker)])[pending addObject:marker];
    BOOL setup=[fm fileExistsAtPath:path(@"private/var/db/.AppleSetupDone")];
    BOOL system=[fm fileExistsAtPath:path(@"System/Library/CoreServices/Finder.app")];
    BOOL recovery=[fm fileExistsAtPath:path(@"System/Installation/CDIS/macOS Utilities.app")] ||
        [fm fileExistsAtPath:path(@"System/Installation/CDIS/Recovery Springboard.app")];
    NSURL *rootURL=[NSURL fileURLWithPath:root isDirectory:YES];
    NSString *volumeUUID=nil;[rootURL getResourceValue:&volumeUUID forKey:NSURLVolumeUUIDStringKey error:NULL];
    struct utsname kernel;BOOL haveKernel=uname(&kernel)==0;
    NSString *kernelBuild=live?SysctlString("kern.osversion"):@"";
    BOOL candidate=![guest isEqual:@"unsupported"]&&setup&&system&&!recovery&&!pending.count;
    NSInteger consoleLocked=live?VIMConsoleLockState():-1;
    NSDictionary *report=@{@"ProtocolVersion":@1,@"Guest":guest,@"ProductVersion":release,@"ProductBuildVersion":build,
        @"VolumeUUID":volumeUUID?:@"",@"BootSessionUUID":live?SysctlString("kern.bootsessionuuid"):@"",@"LiveRoot":@(live),@"KernelBuild":kernelBuild,
        @"KernelMatchesSystem":(live&&[kernelBuild isEqual:build])?@YES:@NO,
        @"ProcessMachine":haveKernel?@(kernel.machine):@"unknown",@"SetupComplete":@(setup),
        @"FinderPresent":@(system),@"DesktopRunning":@(live&&consoleLocked==0&&DesktopRunning()),@"RecoveryToolsPresent":@(recovery),@"InstallerMarkers":pending,
        @"InstalledSystemCandidate":@(candidate),@"Ready":@NO,
        @"ReadinessReason":@"Authenticated guest service and verified patch transaction are required."};
    NSMutableDictionary *evidence=[report mutableCopy];
    evidence[@"OSInstallerMessagesPresent"]=@([fm fileExistsAtPath:path(@".OSInstallerMessages")]);
    if(live) {
        evidence[@"ConsoleStateKnown"]=@(consoleLocked>=0);
        if(consoleLocked>=0)evidence[@"ConsoleLocked"]=@(consoleLocked==1);
        evidence[@"NetworkInterfaces"]=NetworkInterfaces();
        uid_t consoleUID=0;gid_t consoleGID=0;
        CFStringRef consoleName=SCDynamicStoreCopyConsoleUser(NULL,&consoleUID,&consoleGID);
        if(consoleName)CFRelease(consoleName);
        evidence[@"ConsoleUID"]=@(consoleUID);
        SCDynamicStoreRef store=SCDynamicStoreCreate(NULL,CFSTR("VirtualIntelMac guest status"),NULL,NULL);
        if(store) {
            NSMutableDictionary *network=[NSMutableDictionary dictionary];
            for(NSString *key in @[@"State:/Network/Global/IPv4",@"State:/Network/Global/DNS"]) {
                id value=CFBridgingRelease(SCDynamicStoreCopyValue(store,(__bridge CFStringRef)key));
                if(value)network[key]=value;
            }
            NSString *interface=network[@"State:/Network/Global/IPv4"][@"PrimaryInterface"];
            if([interface isKindOfClass:NSString.class]&&interface.length) {
                NSString *key=[NSString stringWithFormat:@"State:/Network/Interface/%@/IPv4",interface];
                id value=CFBridgingRelease(SCDynamicStoreCopyValue(store,(__bridge CFStringRef)key));if(value)network[key]=value;
            }
            evidence[@"Network"]=network;CFRelease(store);
        }
    }
    evidence[@"OpenGLDonorManifestPresent"]=@([fm fileExistsAtPath:path(@"System/Library/Frameworks/OpenGL.framework/Versions/A/Resources/PVGDonorFiles.txt")]);
    NSData *json=[NSJSONSerialization dataWithJSONObject:evidence options:NSJSONWritingPrettyPrinted|NSJSONWritingSortedKeys error:&error];
    if(!json)return 74;
    fwrite(json.bytes,1,json.length,stdout);fputc('\n',stdout);return 0;
}}
