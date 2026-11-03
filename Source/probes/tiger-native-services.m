// Finite native service-policy probe. Read-only on the physical reference;
// explicit enable operations require root inside our virtual machine.
#import <Foundation/Foundation.h>
#import "../tools/opengl-guest-machine.h"
#import "../tools/opengl-launchd-state.h"
#import "../tools/opengl-state-path.h"
#import "../tools/opengl-account-directory.h"
#import "../tools/opengl-tiger-services.h"
#import <DiskArbitration/DiskArbitration.h>
#include <sys/utsname.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <dlfcn.h>
// Signatures/selector names measured in 8R3032 Admin's ObjC1 metadata.
@protocol VIMTigerServices
+ (id)sharedInternetServices;
- (BOOL)sshServerEnabled;
- (BOOL)smbServerEnabled;
- (void)setSSHServerEnabled:(BOOL)enabled;
- (void)setSMBServerEnabled:(BOOL)enabled;
@end
@protocol VIMTigerUser
+ (id)findUserByName:(NSString *)name searchParent:(BOOL)search;
- (BOOL)isSMBCompatible;
- (BOOL)isLocal;
- (int)userID;
- (BOOL)verifyPassword:(NSString *)password;
- (BOOL)setSMBCompatible:(BOOL)enabled password:(NSString *)password;
@end
static BOOL PrivateRecord(NSString *path){
    struct stat st;return !lstat([path fileSystemRepresentation],&st)&&S_ISREG(st.st_mode)&&
        st.st_uid==0&&st.st_nlink==1&&!(st.st_mode&0077)&&st.st_size>0&&st.st_size<=16384;
}
static BOOL OwnedUser(NSString *name,uid_t uid){
    NSString *base=VIM_OPENGL_STATE_PATH;
    NSString *ownerPath=[base stringByAppendingPathComponent:@"Ownership.plist"];
    NSString *accountPath=[base stringByAppendingPathComponent:@"Account.plist"];
    if(!PrivateRecord(ownerPath)||!PrivateRecord(accountPath))return NO;
    NSDictionary *owner=[NSDictionary dictionaryWithContentsOfFile:ownerPath];
    NSDictionary *account=[NSDictionary dictionaryWithContentsOfFile:accountPath];
    struct statfs root;DASessionRef session=DASessionCreate(NULL);
    DADiskRef disk=session&&!statfs("/",&root)?DADiskCreateFromBSDName(NULL,session,root.f_mntfromname):NULL;
    NSDictionary *info=disk?(NSDictionary *)DADiskCopyDescription(disk):nil;
    CFTypeRef value=(CFTypeRef)[info objectForKey:(NSString *)kDADiskDescriptionVolumeUUIDKey];
    NSString *volume=value&&CFGetTypeID(value)==CFUUIDGetTypeID()?(NSString *)CFUUIDCreateString(NULL,(CFUUIDRef)value):nil;
    BOOL owned=volume&&[[owner objectForKey:@"VolumeUUID"] isEqual:volume]&&
        [[account objectForKey:@"VolumeUUID"] isEqual:volume]&&[[owner objectForKey:@"Guest"] isEqual:@"tiger"]&&
        [[account objectForKey:@"MachineIdentifier"] isEqual:[owner objectForKey:@"MachineIdentifier"]]&&
        [[owner objectForKey:@"Username"] isEqual:name]&&[[account objectForKey:@"Username"] isEqual:name]&&
        [[account objectForKey:@"UID"] unsignedIntValue]==uid&&uid>=501;
    [volume release];[info release];if(disk)CFRelease(disk);if(session)CFRelease(session);
    VIMAccountNode *node=owned?VIMAccountLocalNode():nil;
    VIMAccountRecord *record=node?[node recordWithRecordType:kODRecordTypeUsers name:name attributes:nil error:NULL]:nil;
    NSArray *ids=record?[record valuesForAttribute:kODAttributeTypeGUID error:NULL]:nil;
    return owned&&[ids count]==1&&[[ids objectAtIndex:0] isEqual:[account objectForKey:@"GeneratedUID"]];
}
int main(int argc,char **argv){
    NSAutoreleasePool *pool=[NSAutoreleasePool new];struct utsname kernel;
    BOOL inspect=argc==2&&!strcmp(argv[1],"--inspect");
    BOOL inspectUser=argc==3&&!strcmp(argv[1],"--inspect-user");
    BOOL prepareUser=argc==3&&!strcmp(argv[1],"--prepare-owned-smb");
    BOOL ssh=argc==2&&!strcmp(argv[1],"--enable-remote-login");
    BOOL smb=argc==2&&!strcmp(argv[1],"--enable-file-sharing");
    if((!inspect&&!inspectUser&&!prepareUser&&!ssh&&!smb)||uname(&kernel)||strncmp(kernel.release,"8.",2))return 77;
    if(!inspect&&!inspectUser&&(geteuid()!=0||!VIMOpenGLGuestMachine()))return 77;
    void *image=dlopen("/System/Library/PrivateFrameworks/Admin.framework/Versions/A/Admin",RTLD_NOW|RTLD_LOCAL);
    if(inspectUser||prepareUser){
        Class userType=image?NSClassFromString(@"User"):Nil;
        if(!userType||![(id)userType respondsToSelector:@selector(findUserByName:searchParent:)])return 69;
        id<VIMTigerUser> user=[(id)userType findUserByName:[NSString stringWithUTF8String:argv[2]] searchParent:NO];
        if(!user||![(id)user respondsToSelector:@selector(isSMBCompatible)]||
           ![(id)user respondsToSelector:@selector(isLocal)]||![(id)user respondsToSelector:@selector(userID)])return 69;
        if(prepareUser){
            NSString *name=[NSString stringWithUTF8String:argv[2]];
            if(![user isLocal]||!OwnedUser(name,(uid_t)[user userID])||
               ![(id)user respondsToSelector:@selector(setSMBCompatible:password:)]||
               ![(id)user respondsToSelector:@selector(verifyPassword:)])return 77;
            unsigned char secret[512];ssize_t count;size_t used=0;
            while((count=read(0,secret+used,sizeof(secret)-used))>0){used+=(size_t)count;if(used==sizeof(secret))return 64;}
            if(count<0)return 74;
            NSString *password=[[[NSString alloc] initWithBytes:secret length:used encoding:NSUTF8StringEncoding] autorelease];
            volatile unsigned char *wipe=secret;for(size_t i=0;i<sizeof(secret);i++)wipe[i]=0;
            if(!password||![user verifyPassword:password])return 77;
            BOOL before=[user isSMBCompatible];
            BOOL changed=VIMTigerPrepareSMBUser(name,(uid_t)[user userID],password);
            id<VIMTigerUser> check=[(id)userType findUserByName:name searchParent:NO];
            BOOL after=changed&&[check isSMBCompatible]&&[check verifyPassword:password];
            printf("{\"BeforeSMBCompatible\":%s,\"AfterSMBCompatible\":%s,\"PasswordStillVerified\":%s}\n",
                before?"true":"false",after?"true":"false",[check verifyPassword:password]?"true":"false");
            [pool drain];return after?0:1;
        }
        printf("{\"UserFound\":true,\"UserID\":%d,\"Local\":%s,\"NativeSMBCompatible\":%s,\"Changed\":false}\n",
            [user userID],[user isLocal]?"true":"false",[user isSMBCompatible]?"true":"false");
        [pool drain];return 0;
    }
    Class type=image?NSClassFromString(@"InternetServices"):Nil;
    if(!type||![(id)type respondsToSelector:@selector(sharedInternetServices)])return 69;
    id<VIMTigerServices> services=[(id)type sharedInternetServices];
    if(![(id)services respondsToSelector:@selector(sshServerEnabled)]||
       ![(id)services respondsToSelector:@selector(smbServerEnabled)])return 69;
    int configured=(ssh||smb)?VIMTigerEnableNativeServices(smb,ssh):0;
    BOOL sshPolicy=[services sshServerEnabled],smbPolicy=[services smbServerEnabled];
    BOOL sshLoaded=geteuid()==0&&VIMOpenGLSystemJobLoaded(CFSTR("com.openssh.sshd"));
    printf("{\"RemoteLoginPolicy\":%s,\"FileSharingPolicy\":%s,\"SSHJobLoaded\":%s,\"Changed\":%s}\n",
        sshPolicy?"true":"false",smbPolicy?"true":"false",sshLoaded?"true":"false",inspect?"false":"true");
    // The native setter queues launchd publication. A true policy with its
    // job not yet visible is pending (75), not permission failure or success.
    [pool drain];return configured;
}
