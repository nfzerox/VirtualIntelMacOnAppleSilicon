// Read-only desktop evidence for installed pre-Metal guests, including imports
// without an automatic-install receipt. Never authorizes system-file changes.
#import <Foundation/Foundation.h>
#import <SystemConfiguration/SystemConfiguration.h>
#import <IOKit/IOKitLib.h>
#import "opengl-guest-machine.h"
#import "opengl-boot-identity.h"
#import "opengl-desktop-processes.h"
#import "opengl-system-version.h"
#include <ifaddrs.h>
#include <net/if_dl.h>
#include <arpa/inet.h>
#include <sys/sysctl.h>
#include <unistd.h>
int main(void){@autoreleasepool{
    NSDictionary *system=[NSDictionary dictionaryWithContentsOfFile:@"/System/Library/CoreServices/SystemVersion.plist"];
    NSString *version=[system objectForKey:@"ProductVersion"],*guest=VIMOpenGLGuestForSystemVersion(version);
    if(geteuid()!=0||!guest||!VIMOpenGLGuestMachine())return 77;
    uid_t uid=0;gid_t gid=0;NSString *name=(NSString *)SCDynamicStoreCopyConsoleUser(NULL,&uid,&gid);
    BOOL desktop=VIMOpenGLDesktopRunning(uid);
    NSMutableDictionary *interfaces=[NSMutableDictionary dictionary];struct ifaddrs *addresses=NULL;
    if(!getifaddrs(&addresses)){
        for(struct ifaddrs *p=addresses;p;p=p->ifa_next){if(!p->ifa_addr)continue;NSString *key=[NSString stringWithUTF8String:p->ifa_name];
            NSMutableDictionary *entry=[interfaces objectForKey:key];
            if(!entry){entry=[NSMutableDictionary dictionaryWithObjectsAndKeys:key,@"Name",[NSMutableArray array],@"Addresses",nil];[interfaces setObject:entry forKey:key];}
            if(p->ifa_addr->sa_family==AF_INET){char ip[INET_ADDRSTRLEN];if(inet_ntop(AF_INET,&((struct sockaddr_in *)p->ifa_addr)->sin_addr,ip,sizeof(ip))) [[entry objectForKey:@"Addresses"] addObject:[NSString stringWithUTF8String:ip]];}
            if(p->ifa_addr->sa_family==AF_LINK){struct sockaddr_dl *link=(struct sockaddr_dl *)p->ifa_addr;if(link->sdl_alen==6){const unsigned char *m=(const unsigned char *)LLADDR(link);[entry setObject:[NSString stringWithFormat:@"%02x:%02x:%02x:%02x:%02x:%02x",m[0],m[1],m[2],m[3],m[4],m[5]] forKey:@"MACAddress"];}}
        }freeifaddrs(addresses);
    }
    NSString *boot=VIMOpenGLBootSessionUUID(NULL);if(!boot)return 1;
    NSDictionary *report=[NSDictionary dictionaryWithObjectsAndKeys:@1,@"ProtocolVersion",[NSNumber numberWithBool:NO],@"Ready",guest,@"Guest",version,@"ProductVersion",[system objectForKey:@"ProductBuildVersion"],@"ProductBuildVersion",boot,@"BootSessionUUID",[NSNumber numberWithUnsignedInt:uid],@"ConsoleUID",name?:@"",@"ConsoleUsername",[NSNumber numberWithBool:desktop],@"DesktopRunning",[interfaces allValues],@"NetworkInterfaces",[NSNumber numberWithBool:VIMOpenGLControlIdleSleepGuard(boot)],@"IdleSleepGuardRegistered",nil];
    NSData *json=[NSJSONSerialization dataWithJSONObject:report options:0 error:NULL];[name release];if(!json)return 1;
    fwrite([json bytes],1,[json length],stdout);fputc('\n',stdout);return 0;
}}
