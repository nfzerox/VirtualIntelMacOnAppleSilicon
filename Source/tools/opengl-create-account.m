// First-install only. Native directory provisioning, not Buddy injection.
// The recovery bootstrap authorizes exactly one new volume/account transaction.
// JSON password arrives through stdin; never print or persist the request.
#import <Foundation/Foundation.h>
#ifdef VIM_MODERN_ACCOUNT_SETUP
#import "guest-system-version.h"
#import "modern-setup-identity.h"
#import "opengl-desktop-processes.h"
#import <SystemConfiguration/SystemConfiguration.h>
#else
#import "opengl-system-version.h"
#import <SystemConfiguration/SystemConfiguration.h>
#endif
#import "opengl-account-directory.h"
#import <DiskArbitration/DiskArbitration.h>
#import <IOKit/IOKitLib.h>
#import <CommonCrypto/CommonDigest.h>
#include "opengl-launchd-state.h"
#import "opengl-login-policy.h"
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1050
#import "opengl-sharing.h"
#else
#import "opengl-tiger-services.h"
#endif
#import "opengl-first-login.h"
#import "opengl-guest-machine.h"
#import "opengl-boot-identity.h"
#include "opengl-control-network.h"
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1060
#import "opengl-local-kerberos.h"
#endif
#include "opengl-state-path.h"
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <pwd.h>
#include <errno.h>
#include <sys/sysctl.h>

static NSString *const State=VIM_OPENGL_STATE_PATH;
static int Failure(const char *stage) {fprintf(stderr,"Automatic account setup could not complete: %s. No existing account was replaced.\n",stage);return 1;}
static int DirectoryFailure(const char *stage,NSError *error) {
    // Do not include OD's userInfo/description: those may contain authentication
    // items. The numeric code is enough to diagnose the native operation.
    fprintf(stderr,"Directory service error code: %ld.\n",(long)[error code]);return Failure(stage);
}
static BOOL UUID(id value) {CFUUIDRef uuid=[value isKindOfClass:[NSString class]]?CFUUIDCreateFromString(NULL,(CFStringRef)value):NULL;if(uuid)CFRelease(uuid);return uuid!=NULL;}
static BOOL Owned(NSString *path,BOOL directory) {
    struct stat st;return !lstat([path fileSystemRepresentation],&st)&&st.st_uid==0&&
        !(st.st_mode&0077)&&(directory?S_ISDIR(st.st_mode):(S_ISREG(st.st_mode)&&st.st_nlink==1));
}
static NSString *RootUUID(void) {
#ifdef VIM_MODERN_ACCOUNT_SETUP
    // Big Sur boots a snapshot. DA names that snapshot, while the existing
    // host/guest installation protocol identifies the mounted system volume.
    // Use the same native resource key as guest-status/guest-install, rather
    // than comparing two different, individually valid APFS UUIDs.
    NSString *uuid=nil;
    [[NSURL fileURLWithPath:@"/"] getResourceValue:&uuid forKey:NSURLVolumeUUIDStringKey error:NULL];
    return uuid;
#else
    DASessionRef session=DASessionCreate(NULL);if(!session)return nil;
    DADiskRef disk=DADiskCreateFromVolumePath(NULL,session,(CFURLRef)[NSURL fileURLWithPath:@"/"]);
    NSDictionary *info=disk?(NSDictionary *)DADiskCopyDescription(disk):nil;
    CFUUIDRef uuid=(CFUUIDRef)[info objectForKey:(NSString *)kDADiskDescriptionVolumeUUIDKey];
    NSString *value=uuid?[(NSString *)CFUUIDCreateString(NULL,uuid) autorelease]:nil;
    [info release];if(disk)CFRelease(disk);CFRelease(session);return value;
#endif
}
static BOOL Save(NSDictionary *record,NSString *path) {
    NSError *error=nil;NSData *bytes=[NSPropertyListSerialization dataWithPropertyList:record format:NSPropertyListXMLFormat_v1_0 options:0 error:&error];
    // The parent is root-only and locked; atomic replacement cannot expose data
    // through a preexisting user-created symlink or a world-readable temporary.
    return bytes&&[bytes writeToFile:path options:NSDataWritingAtomic error:&error]&&chmod([path fileSystemRepresentation],0600)==0;
}
#ifdef VIM_MODERN_ACCOUNT_SETUP
static BOOL AdoptModernOwnership(NSDictionary *request,NSString *guest,NSString *volume) {
    if(Owned(State,YES))return YES;
    NSDictionary *prepared=nil;
    if(Owned([VIMModernInstallerOwnership stringByDeletingLastPathComponent],YES)&&Owned(VIMModernInstallerOwnership,NO))
        prepared=[NSDictionary dictionaryWithContentsOfFile:VIMModernInstallerOwnership];
    if(!prepared){
        // Native Big Sur clean installation discards the preseeded Library.
        // Reconstruct the non-secret policy from the app's durable installer
        // dispatch, but only on that same disk, before any completed setup.
        // The actual account creation below still refuses an existing user.
        NSString *target=[request objectForKey:@"TargetVolumeUUID"],*partition=[request objectForKey:@"TargetDiskUUID"];
        BOOL sameDisk=UUID(partition)?VIMModernRootStoreMatches(volume,partition):
            (UUID(target)&&[target caseInsensitiveCompare:volume]==NSOrderedSame);
        if([[NSFileManager defaultManager] fileExistsAtPath:VIMModernInstallerOwnership]||
           ![[request objectForKey:@"NativeInstallDispatched"] isEqual:@YES]||!sameDisk||
           [[NSFileManager defaultManager] fileExistsAtPath:@"/private/var/db/.AppleSetupDone"])return NO;
        NSMutableDictionary *policy=[NSMutableDictionary dictionaryWithDictionary:request];
        [policy removeObjectForKey:@"Password"];[policy removeObjectForKey:@"NativeInstallDispatched"];
        [policy setObject:@YES forKey:@"Enabled"];[policy setObject:target forKey:@"VolumeUUID"];
        if(!partition)[policy setObject:@"" forKey:@"TargetDiskUUID"];
        prepared=policy;
    }
    if(![[prepared objectForKey:@"FormatVersion"] isEqual:@1]||![[prepared objectForKey:@"Enabled"] isEqual:@YES]||
       ![[prepared objectForKey:@"Guest"] isEqual:guest]||
       ![[prepared objectForKey:@"MachineIdentifier"] isEqual:[request objectForKey:@"MachineIdentifier"]]||
       ![[prepared objectForKey:@"Username"] isEqual:[request objectForKey:@"Username"]]||
       ![[prepared objectForKey:@"VolumeUUID"] isEqual:[request objectForKey:@"TargetVolumeUUID"]]||
       !(UUID([prepared objectForKey:@"TargetDiskUUID"])?VIMModernRootStoreMatches(volume,[prepared objectForKey:@"TargetDiskUUID"]):
         [[prepared objectForKey:@"VolumeUUID"] caseInsensitiveCompare:volume]==NSOrderedSame))return NO;
    // Do not turn the shared var/db ancestors into private directories. Only
    // the authorization/transaction directory itself contains private state.
    if(![[NSFileManager defaultManager] createDirectoryAtPath:State withIntermediateDirectories:YES
        attributes:[NSDictionary dictionaryWithObject:@0755 forKey:NSFilePosixPermissions] error:NULL]||
       chmod([State fileSystemRepresentation],0700))return NO;
    NSMutableDictionary *owner=[[prepared mutableCopy] autorelease];
    [owner setObject:[prepared objectForKey:@"VolumeUUID"] forKey:@"TargetVolumeUUID"];
    [owner setObject:volume forKey:@"VolumeUUID"];
    return Save(owner,[State stringByAppendingPathComponent:@"Ownership.plist"]);
}
#endif
static int Run(NSString *path,NSArray *arguments) {
    NSTask *task=[[[NSTask alloc] init] autorelease];[task setLaunchPath:path];[task setArguments:arguments];
    [task setStandardInput:[NSFileHandle fileHandleWithNullDevice]];
    [task setStandardOutput:[NSFileHandle fileHandleWithNullDevice]];
    [task setStandardError:[NSFileHandle fileHandleWithNullDevice]];
    @try{[task launch];[task waitUntilExit];return [task terminationStatus];}@catch(NSException *exception){return -1;}
}
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1050
static BOOL JobLoaded(CFStringRef label){return VIMOpenGLSystemJobLoaded(label);}
#endif
static NSString *First(VIMAccountRecord *record,NSString *attribute) {
    NSArray *values=[record valuesForAttribute:attribute error:NULL];return [values count]?[values objectAtIndex:0]:nil;
}
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1050
static NSArray *HomeShares(VIMAccountNode *node,NSString *name){return VIMAccountHomeShares(node,name);}
static BOOL ShareFailure(const char *stage,NSError *error){
    // Cold failure metadata only. Never log directory object descriptions,
    // account names, authentication values or the supplied request.
    fprintf(stderr,"File share operation failed: %s (directory code %ld).\n",stage,(long)[error code]);return NO;
}
static BOOL ShareHome(VIMAccountNode *node,NSString *name,NSString *home){
    NSError *issue=nil;
    NSArray *shares=HomeShares(node,name);if(!shares||[shares count]>1)return ShareFailure("query",nil);
    if(access("/usr/sbin/sharing",X_OK)){
        // Lion has no sharing CLI. Use its native SharePoints record schema
        // (confirmed on 11G63), limited to this transaction's exact home.
        VIMAccountRecord *share=[shares count]?[shares objectAtIndex:0]:nil;
        if(share){
            NSArray *paths=[share valuesForAttribute:@"dsAttrTypeNative:directory_path" error:&issue];
            NSString *path=[paths count]?[paths objectAtIndex:0]:nil;
            if(![path isEqual:home]){
                fprintf(stderr,"File share home metadata: count=%lu actual-length=%lu expected-length=%lu.\n",
                    (unsigned long)[paths count],(unsigned long)[path length],(unsigned long)[home length]);
                return ShareFailure("home identity",issue);
            }
        }
        NSDictionary *attributes=[NSDictionary dictionaryWithObjectsAndKeys:
            [NSArray arrayWithObject:home],@"dsAttrTypeNative:directory_path",
            [NSArray arrayWithObject:name],@"dsAttrTypeNative:smb_name",
            [NSArray arrayWithObject:@"1"],@"dsAttrTypeNative:smb_shared",
            [NSArray arrayWithObject:@"0"],@"dsAttrTypeNative:smb_guestaccess",nil];
        if(!share){
            CFUUIDRef identifier=CFUUIDCreate(NULL);
            NSString *guid=[(NSString *)CFUUIDCreateString(NULL,identifier) autorelease];CFRelease(identifier);
            NSMutableDictionary *created=[[attributes mutableCopy] autorelease];
            [created setObject:[NSArray arrayWithObject:guid] forKey:kODAttributeTypeGUID];
            share=[node createRecordWithRecordType:kODRecordTypeSharePoints name:name attributes:created error:&issue];
        }
        else for(NSString *key in attributes)
            if(![share setValue:[attributes objectForKey:key] forAttribute:key error:&issue])return ShareFailure("attribute write",issue);
        if(!share||![share synchronizeAndReturnError:&issue])return ShareFailure("record publish",issue);
        shares=HomeShares(node,name);if([shares count]!=1)return ShareFailure("read-back query",nil);
        share=[shares objectAtIndex:0];
        BOOL verified=[First(share,@"dsAttrTypeNative:directory_path") isEqual:home]&&
            [First(share,@"dsAttrTypeNative:smb_name") isEqual:name]&&
            [First(share,@"dsAttrTypeNative:smb_shared") isEqual:@"1"]&&
            [First(share,@"dsAttrTypeNative:smb_guestaccess") isEqual:@"0"];
        return verified?YES:ShareFailure("read-back attributes",nil);
    }
    if([shares count]){
        VIMAccountRecord *share=[shares objectAtIndex:0];
        // An interrupted native enable may already have published this share.
        // Reuse only this exact account's home, never edit a name collision.
        if(![First(share,@"dsAttrTypeNative:directory_path") isEqual:home])return ShareFailure("command home identity",nil);
        if(![First(share,@"dsAttrTypeNative:smb_shared") isEqual:@"1"]||
           ![First(share,@"dsAttrTypeNative:smb_guestaccess") isEqual:@"0"]){
            if(Run(@"/usr/sbin/sharing",[NSArray arrayWithObjects:@"-e",name,@"-s",@"001",@"-g",@"000",nil]))return ShareFailure("native edit command",nil);
        }
    }else if(Run(@"/usr/sbin/sharing",[NSArray arrayWithObjects:@"-a",home,@"-n",name,@"-s",@"001",@"-g",@"000",nil]))return ShareFailure("native create command",nil);
    shares=HomeShares(node,name);if([shares count]!=1)return ShareFailure("command read-back query",nil);
    VIMAccountRecord *share=[shares objectAtIndex:0];
    BOOL verified=[First(share,@"dsAttrTypeNative:directory_path") isEqual:home]&&
        [First(share,@"dsAttrTypeNative:smb_name") isEqual:name]&&
        [First(share,@"dsAttrTypeNative:smb_shared") isEqual:@"1"]&&
        [First(share,@"dsAttrTypeNative:smb_guestaccess") isEqual:@"0"];
    return verified?YES:ShareFailure("command read-back attributes",nil);
}
#endif
static NSString *SHA(NSData *data){
    unsigned char hash[CC_SHA256_DIGEST_LENGTH];CC_SHA256_CTX context;CC_SHA256_Init(&context);
    CC_SHA256_Update(&context,[data bytes],(CC_LONG)[data length]);CC_SHA256_Final(hash,&context);
    NSMutableString *result=[NSMutableString string];
    for(unsigned i=0;i<sizeof(hash);i++)[result appendFormat:@"%02x",hash[i]];return result;
}
static BOOL LoginOnce(NSString *name,NSString *password,NSMutableDictionary *journal,NSString *path,BOOL allowPendingSync){
    // Actual Mavericks logind (13F1911), function 0x1000025b6: NUL-terminated
    // UTF-8, random padding to the NEXT multiple of 12, XOR repeating 11 bytes.
    // This is Apple's reversible root-only login handoff, NOT encryption. It
    // is removed after our first verified desktop. No OS instruction is patched.
    static const unsigned char key[]={0x7d,0x89,0x52,0x23,0xd2,0xbc,0xdd,0xea,0xa3,0xb9,0x1f};
    BOOL present=[[NSFileManager defaultManager] fileExistsAtPath:@"/etc/kcpassword"];
    if(present&&(![journal objectForKey:@"AutoLoginSHA256"]||!Owned(@"/etc/kcpassword",NO)))return NO;
    NSData *existing=present?[NSData dataWithContentsOfFile:@"/etc/kcpassword"]:nil;
    if(existing&&![[journal objectForKey:@"AutoLoginSHA256"] isEqual:SHA(existing)])return NO;
    if(!existing){
        NSData *plain=[password dataUsingEncoding:NSUTF8StringEncoding];NSUInteger count=([plain length]/12+1)*12;
        NSMutableData *encoded=[NSMutableData dataWithLength:count];unsigned char *bytes=[encoded mutableBytes];arc4random_buf(bytes,count);
        memcpy(bytes,[plain bytes],[plain length]);bytes[[plain length]]=0;
        for(NSUInteger i=0;i<count;i++)bytes[i]^=key[i%sizeof(key)];
        [journal setObject:SHA(encoded) forKey:@"AutoLoginSHA256"];
        if(![[journal objectForKey:@"Phase"] isEqual:@"autologin-armed"])
            [journal setObject:@"login-credential-prepared" forKey:@"Phase"];
        if(!Save(journal,path)||![encoded writeToFile:@"/etc/kcpassword" options:NSDataWritingAtomic error:NULL]||chmod("/etc/kcpassword",0600))return NO;
        [encoded resetBytesInRange:NSMakeRange(0,count)];
    }
    return VIMOpenGLSetLoginUserWithPendingPublication(name,allowPendingSync);
}
int main(int argc,char **argv) {@autoreleasepool {
#ifdef VIM_MODERN_ACCOUNT_SETUP
    BOOL status=argc==2&&!strcmp(argv[1],"--status"),finish=argc==2&&!strcmp(argv[1],"--finish-first-login");
    if(argc!=2||(!status&&!finish&&strcmp(argv[1],"--preflight")&&strcmp(argv[1],"--create")))return 64;
#else
    if(argc!=2||(strcmp(argv[1],"--preflight")&&strcmp(argv[1],"--create")&&
                 strcmp(argv[1],"--prepare-file-sharing")&&strcmp(argv[1],"--recover-first-login")))return 64;
#endif
    BOOL sharingOnly=!strcmp(argv[1],"--prepare-file-sharing"),create=!strcmp(argv[1],"--create")||sharingOnly;
    NSString *version=[[NSDictionary dictionaryWithContentsOfFile:@"/System/Library/CoreServices/SystemVersion.plist"] objectForKey:@"ProductVersion"];
#ifdef VIM_MODERN_ACCOUNT_SETUP
    NSString *guest=VIMGuestForSystemVersion(version);
    if(geteuid()!=0||![[NSArray arrayWithObjects:@"mojave",@"catalina",@"bigsur",nil] containsObject:guest]||!VIMModernSetupGuestMachine())return 77;
#else
    if(geteuid()!=0||!VIMOpenGLGuestForSystemVersion(version)||!VIMOpenGLGuestMachine())return 77;
#endif
    umask(0077);
    NSMutableData *bytes=[NSMutableData data];unsigned char chunk[2048];ssize_t count;
    while((count=read(STDIN_FILENO,chunk,sizeof(chunk)))!=0){
        if(count<0){if(errno==EINTR)continue;return Failure("reading private input");}
        if([bytes length]+(NSUInteger)count>16384)return Failure("input is too large");
        [bytes appendBytes:chunk length:(NSUInteger)count];
    }
    id request=[NSJSONSerialization JSONObjectWithData:bytes options:0 error:NULL];
    if(![request isKindOfClass:[NSDictionary class]])return Failure("invalid private input");
    NSString *name=[request objectForKey:@"Username"],*password=[request objectForKey:@"Password"];
    NSString *machine=[request objectForKey:@"MachineIdentifier"],*volume=[request objectForKey:@"VolumeUUID"];
    NSRegularExpression *pattern=[NSRegularExpression regularExpressionWithPattern:@"^[a-z][a-z0-9_-]{0,30}$" options:0 error:NULL];
    NSString *rootUUID=RootUUID();
    if(![name isKindOfClass:[NSString class]]||[pattern numberOfMatchesInString:name options:0 range:NSMakeRange(0,[name length])]!=1||
       [[NSArray arrayWithObjects:@"root",@"daemon",@"nobody",@"guest",@"wheel",nil] containsObject:name]||
       ![password isKindOfClass:[NSString class]]||[password lengthOfBytesUsingEncoding:NSUTF8StringEncoding]>511||
       [password rangeOfString:[NSString stringWithFormat:@"%C",(unichar)0]].location!=NSNotFound||
       !UUID(machine)||!UUID(volume)||!rootUUID||[volume caseInsensitiveCompare:rootUUID]!=NSOrderedSame)return Failure("invalid account or target identity");
    // Preflight is explicitly read-only, useful on a configured development VM.
    VIMAccountNode *node=VIMAccountLocalNode();
    if(!node)return Failure("opening the local directory");
    if(!create&&!strcmp(argv[1],"--preflight")){puts("{\"TargetVerified\":true,\"DirectoryAvailable\":true,\"AccountChanged\":false}");return 0;}
#ifdef VIM_MODERN_ACCOUNT_SETUP
    if(create&&!AdoptModernOwnership(request,guest,volume))return Failure("the installed disk has no matching native installer authorization");
#endif
    if(!Owned(State,YES))return Failure("missing bootstrap authorization");
    NSString *ownershipPath=[State stringByAppendingPathComponent:@"Ownership.plist"];
    NSDictionary *ownership=Owned(ownershipPath,NO)?[NSDictionary dictionaryWithContentsOfFile:ownershipPath]:nil;
#ifdef VIM_MODERN_ACCOUNT_SETUP
    if(![[ownership objectForKey:@"Guest"] isEqual:guest]||![[ownership objectForKey:@"Enabled"] boolValue])
        return Failure("the modern account operation is not authorized");
#endif
    if(![[ownership objectForKey:@"FormatVersion"] isEqual:[NSNumber numberWithInt:1]]||
       ![[ownership objectForKey:@"MachineIdentifier"] isEqual:machine]||
       ![[ownership objectForKey:@"VolumeUUID"] isEqual:volume]||
       ![[ownership objectForKey:@"Username"] isEqual:name])return Failure("bootstrap identity does not match");
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
    // Tiger's native Admin account operation is distinct from the Darwin9
    // credential repair. Never send the latter to a preexisting Tiger user.
    if(sharingOnly)return Failure("the file-sharing repair does not apply to this OS");
#endif
    NSString *lockPath=[State stringByAppendingPathComponent:@"Account.lock"];
    int lock=open([lockPath fileSystemRepresentation],O_CREAT|O_RDWR|O_NOFOLLOW|O_CLOEXEC,0600);
    if(lock<0||!Owned(lockPath,NO)||flock(lock,LOCK_EX|LOCK_NB))return Failure("another setup operation is running");
    NSString *journalPath=[State stringByAppendingPathComponent:@"Account.plist"];
    BOOL hasJournal=[[NSFileManager defaultManager] fileExistsAtPath:journalPath];
    NSMutableDictionary *journal=hasJournal&&Owned(journalPath,NO)?[[[NSDictionary dictionaryWithContentsOfFile:journalPath] mutableCopy] autorelease]:nil;
    VIMAccountRecord *record=[node recordWithRecordType:kODRecordTypeUsers name:name attributes:[NSArray arrayWithObject:kODAttributeTypeAllAttributes] error:NULL];
    if(hasJournal&&(!journal||![[journal objectForKey:@"MachineIdentifier"] isEqual:machine]||
       ![[journal objectForKey:@"VolumeUUID"] isEqual:volume]||![[journal objectForKey:@"Username"] isEqual:name]||
       !UUID([journal objectForKey:@"GeneratedUID"])||![[journal objectForKey:@"UID"] isKindOfClass:[NSNumber class]]||
       [[journal objectForKey:@"UID"] unsignedIntValue]<501||[[journal objectForKey:@"UID"] unsignedIntValue]>=60000))return Failure("saved transaction does not match");
#ifdef VIM_MODERN_ACCOUNT_SETUP
    if(status||finish){
        if(!journal||!record)return 75;
        uid_t identifier=[[journal objectForKey:@"UID"] unsignedIntValue];
        if(![First(record,kODAttributeTypeGUID) isEqual:[journal objectForKey:@"GeneratedUID"]]||
           [First(record,kODAttributeTypeUniqueID) intValue]!=(int)identifier)return Failure("the owned account identity changed");
        if(finish&&![[journal objectForKey:@"Phase"] isEqual:@"complete"]){
            uid_t consoleUID=0;NSString *console=(NSString *)SCDynamicStoreCopyConsoleUser(NULL,&consoleUID,NULL);
            BOOL desktop=consoleUID==identifier&&[console isEqual:name]&&VIMOpenGLDesktopRunning(identifier);[console release];
            if(!desktop)return 75;
            if([[journal objectForKey:@"FirstLoginCookieIntent"] boolValue]&&VIMOpenGLFirstLoginCookie(name,identifier,2)<0)
                return Failure("retiring native first-login policy");
            if([[ownership objectForKey:@"FileSharing"] boolValue]){
                int result=VIMOpenGLConfigureFileSharing();if(result)return result;
            }
            BOOL ours=[VIMOpenGLLoginUser() isEqual:name]&&Owned(@"/etc/kcpassword",NO)&&
                [[journal objectForKey:@"AutoLoginSHA256"] isEqual:SHA([NSData dataWithContentsOfFile:@"/etc/kcpassword"])];
            if(ours&&![[ownership objectForKey:@"AutomaticLogin"] boolValue]&&
               (!VIMOpenGLSetLoginUser(nil)||unlink("/etc/kcpassword")))return Failure("retiring the one-time login credential");
            [journal setObject:@"complete" forKey:@"Phase"];
            if(!Save(journal,journalPath))return Failure("saving the first desktop handoff");
        }
        NSDictionary *result=@{@"Phase":[journal objectForKey:@"Phase"]?:@"pending",@"MachineIdentifier":machine,@"VolumeUUID":volume,
            @"Username":name,@"UID":@(identifier),@"FirstLoginPolicyBootSessionUUID":[journal objectForKey:@"FirstLoginPolicyBootSessionUUID"]?:@""};
        NSData *json=[NSJSONSerialization dataWithJSONObject:result options:0 error:NULL];fwrite(json.bytes,1,json.length,stdout);puts("");close(lock);return 0;
    }
#endif
#ifndef VIM_MODERN_ACCOUNT_SETUP
    if(!strcmp(argv[1],"--recover-first-login")){
        NSString *boot=VIMOpenGLBootSessionUUID(NULL);
        uid_t consoleUID=0;
        CFStringRef console=SCDynamicStoreCopyConsoleUser(NULL,&consoleUID,NULL);
        if(console)CFRelease(console);
        if(!record||!journal||![[ownership objectForKey:@"Guest"] isEqual:@"sierra"]||
           ![First(record,kODAttributeTypeGUID) isEqual:[journal objectForKey:@"GeneratedUID"]]||
           [First(record,kODAttributeTypeUniqueID) intValue]!=[[journal objectForKey:@"UID"] intValue]||
           !VIMSierraLoginHandoffMissing(version,ownership,journal,boot,consoleUID))return 75;
        if(![record verifyPassword:password error:NULL])return Failure("verifying the first-login account");
        [journal setObject:[NSNumber numberWithBool:YES] forKey:@"LoginHandoffRecovered"];
        if(!Save(journal,journalPath)||!LoginOnce(name,password,journal,journalPath,NO))
            return Failure("restoring automatic login after the first restart");
        [journal setObject:boot forKey:@"FirstLoginPolicyBootSessionUUID"];
        if(!Save(journal,journalPath))return Failure("saving the repaired first-login handoff");
        puts("{\"RestartRequired\":true}");close(lock);return 0;
    }
#endif
    if(sharingOnly&&!journal)return Failure("the file-sharing transaction is unavailable");
    if(!journal){
        if(record||getpwnam([name UTF8String])||[[NSFileManager defaultManager] fileExistsAtPath:@"/var/db/.AppleSetupDone"])
            return Failure("the system already has an account or completed Setup Assistant");
        uid_t identifier=501;while(identifier<60000&&getpwuid(identifier))identifier++;
        if(identifier>=60000)return Failure("no local account identifier is available");
        CFUUIDRef uuid=CFUUIDCreate(NULL);NSString *guid=[(NSString *)CFUUIDCreateString(NULL,uuid) autorelease];CFRelease(uuid);
        journal=[NSMutableDictionary dictionaryWithObjectsAndKeys:[NSNumber numberWithInt:1],@"FormatVersion",machine,@"MachineIdentifier",volume,@"VolumeUUID",
            name,@"Username",guid,@"GeneratedUID",[NSNumber numberWithUnsignedInt:identifier],@"UID",@"planned",@"Phase",nil];
        if(!Save(journal,journalPath))return Failure("saving the account transaction");
    }
    NSString *uid=[[journal objectForKey:@"UID"] stringValue],*guid=[journal objectForKey:@"GeneratedUID"];
    if(record){
        if(![First(record,kODAttributeTypeGUID) isEqual:guid]||![First(record,kODAttributeTypeUniqueID) isEqual:uid])
            return Failure("an unrelated account has the requested name");
    }else{
        if(sharingOnly)return Failure("the owned file-sharing account is unavailable");
        if(getpwuid((uid_t)[uid intValue]))return Failure("the planned account identifier is now occupied");
    }
    if(!sharingOnly&&[[ownership objectForKey:@"Enabled"] boolValue]){
        int reserved=VIMOpenGLReserveControlNetworkPorts();
        if(reserved)return reserved==75?75:Failure("reserving the virtual Mac's control ports");
    }
    if(!record){
        NSDictionary *attributes=[NSDictionary dictionaryWithObjectsAndKeys:
            [NSArray arrayWithObject:guid],kODAttributeTypeGUID,[NSArray arrayWithObject:uid],kODAttributeTypeUniqueID,
            [NSArray arrayWithObject:@"20"],kODAttributeTypePrimaryGroupID,[NSArray arrayWithObject:name],kODAttributeTypeFullName,
            [NSArray arrayWithObject:[@"/Users/" stringByAppendingString:name]],kODAttributeTypeNFSHomeDirectory,
            [NSArray arrayWithObject:@"/bin/bash"],kODAttributeTypeUserShell,
            [NSArray arrayWithObject:@"********"],kODAttributeTypePassword,nil];
        record=[node createRecordWithRecordType:kODRecordTypeUsers name:name attributes:attributes error:NULL];
        if(!record)return Failure("creating the authorized local account");
    }
    if(sharingOnly){
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
        // Restrict repair to the original completed automatic setup's exact
        // account/volume/GUID. Never rearm login or change its password, phase,
        // sharing-job enablement, or any other account.
        if(![version hasPrefix:@"10.5."]||![[ownership objectForKey:@"FileSharing"] boolValue]||
           ![[journal objectForKey:@"FileSharingAccountPrepared"] boolValue]||
           ![[NSArray arrayWithObjects:@"complete",@"autologin-armed",nil] containsObject:[journal objectForKey:@"Phase"]?:@""])
            return Failure("the file-sharing repair is not authorized");
        NSError *issue=nil;
        if(![record enableWindowsSharingWithPassword:password error:&issue])return DirectoryFailure("preparing native local Windows sharing",issue);
        [journal setObject:[NSNumber numberWithInt:1] forKey:@"FileSharingCredentialVersion"];
        if(!Save(journal,journalPath))return Failure("saving native file-sharing authentication");
        puts("{\"FileSharingCredentialPrepared\":true,\"AccountReplaced\":false,\"RestartRequired\":false}");close(lock);return 0;
#else
        return Failure("the file-sharing repair does not apply to this OS");
#endif
    }
    // A Password attribute is not a password-setting operation on Mavericks'
    // local node: it can store the supplied string literally, without creating
    // AuthenticationAuthority or ShadowHashData. Never put a secret in that
    // attribute. Use OD's privileged password API, which creates native hashes.
    // Resume only the exact UID/GUID owned by the still-planned transaction.
    NSError *directoryError=nil;
    if([[journal objectForKey:@"Phase"] isEqual:@"planned"]){
        if(![record setValue:@"********" forAttribute:kODAttributeTypePassword error:&directoryError]||
           ![record synchronizeAndReturnError:&directoryError])return DirectoryFailure("initializing password authentication",directoryError);
        if(![record verifyPassword:password error:NULL]&&
           ![record changePassword:nil toPassword:password error:&directoryError])return DirectoryFailure("setting the requested password",directoryError);
    }
    if(![record verifyPassword:password error:&directoryError])return DirectoryFailure("verifying the requested password",directoryError);
    VIMAccountRecord *admin=[node recordWithRecordType:kODRecordTypeGroups name:@"admin" attributes:nil error:NULL];
    if(!admin||![admin addMemberRecord:record error:NULL]||![record synchronizeAndReturnError:NULL])return Failure("administrator group membership");
    if(Run(@"/usr/sbin/createhomedir",[NSArray arrayWithObjects:@"-c",@"-u",name,nil]))return Failure("creating the home folder");
    NSString *home=[@"/Users/" stringByAppendingString:name];struct stat homeInfo;
    if(lstat([home fileSystemRepresentation],&homeInfo)||!S_ISDIR(homeInfo.st_mode)||homeInfo.st_uid!=(uid_t)[uid intValue])return Failure("verifying home ownership");
    [journal setObject:@"account-created" forKey:@"Phase"];
    if(!Save(journal,journalPath))return Failure("saving account completion");
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1050
    if([[ownership objectForKey:@"FileSharing"] boolValue]&&![[journal objectForKey:@"FileSharingConfigured"] boolValue]){
        int ready=VIMOpenGLFileSharingReadiness();
        if(ready)return ready==75?75:Failure("reading native file-sharing preferences");
        // Only this newly provisioned, GUID-matched account gets the SMB hash.
        // Do not enable it globally or alter any preexisting user's password.
        if(![[journal objectForKey:@"FileSharingAccountPrepared"] boolValue]){
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
            if(![record enableWindowsSharingWithPassword:password error:&directoryError])
                return DirectoryFailure("preparing native local Windows sharing",directoryError);
#else
            if(Run(@"/usr/bin/pwpolicy",[NSArray arrayWithObjects:@"-u",name,@"-sethashtypes",@"SMB-NT",@"on",nil]))
                return Failure("enabling this account's SMB password hash");
            // Modern OD treats resetting an already initialized password as
            // a different authorization from changing it with the verified
            // current credential. Use that credential to regenerate SMB-NT,
            // not a second nil-old-password administrator reset.
#ifdef VIM_MODERN_ACCOUNT_SETUP
            if(![record changePassword:password toPassword:password error:&directoryError])
#else
            if(![record changePassword:nil toPassword:password error:&directoryError])
#endif
                return DirectoryFailure("preparing this account's SMB authentication",directoryError);
#endif
            if(!ShareHome(node,name,home))return Failure("creating and confirming this account's file share");
            [journal setObject:[NSNumber numberWithBool:YES] forKey:@"FileSharingAccountPrepared"];
            [journal setObject:[NSNumber numberWithInt:1] forKey:@"FileSharingCredentialVersion"];
            if(!Save(journal,journalPath))return Failure("saving file-sharing account setup");
        }
        if([version hasPrefix:@"10.5."]||[version hasPrefix:@"10.6."]){
            // Period first-boot SharePoints services depend on native desktop
            // initialization. Do not hold account/autologin creation behind
            // that service: prepare only the exact account/share here, then
            // use the existing verified-desktop finalizer to enable the job.
            [journal setObject:[NSNumber numberWithBool:YES] forKey:@"FileSharingDeferredUntilDesktop"];
        }else{
            int sharing=VIMOpenGLConfigureFileSharing();
            if(sharing)return sharing==75?75:Failure("configuring and confirming native file sharing");
            [journal setObject:[NSNumber numberWithBool:YES] forKey:@"FileSharingConfigured"];
        }
        if(!Save(journal,journalPath))return Failure("saving file-sharing setup");
    }
#else
    if([[ownership objectForKey:@"FileSharing"] boolValue]&&![[journal objectForKey:@"FileSharingAccountPrepared"] boolValue]){
        if(!VIMTigerPrepareSMBUser(name,(uid_t)[uid intValue],password))
            return Failure("preparing and verifying native Tiger SMB authentication");
        [journal setObject:[NSNumber numberWithBool:YES] forKey:@"FileSharingAccountPrepared"];
        [journal setObject:[NSNumber numberWithInt:1] forKey:@"FileSharingCredentialVersion"];
        // Complete native service publication after the actual user desktop,
        // without blocking the account/autologin on a first-boot helper IPC.
        [journal setObject:[NSNumber numberWithBool:YES] forKey:@"FileSharingDeferredUntilDesktop"];
        if(!Save(journal,journalPath))return Failure("saving native Tiger file-sharing preparation");
    }
#endif
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1060
    if([[ownership objectForKey:@"FileSharing"] boolValue]&&
       ([version hasPrefix:@"10.11."]||[version hasPrefix:@"10.12."]||[version hasPrefix:@"10.13."]
#ifdef VIM_MODERN_ACCOUNT_SETUP
        ||YES
#endif
       )&&
       ![[journal objectForKey:@"LocalKerberosPrepared"] boolValue]){
        // Start native sharing before waiting for its first-boot KDC record.
        // The UID/GUID and requested password were verified above; never apply
        // this operation to an unrelated/preexisting user.
        int kerberos=VIMOpenGLPrepareLocalKerberos(node,record,name,password,&directoryError);
        if(kerberos)return kerberos==75?75:DirectoryFailure("preparing native file-sharing credentials",directoryError);
        [journal setObject:[NSNumber numberWithBool:YES] forKey:@"LocalKerberosPrepared"];
        if(!Save(journal,journalPath))return Failure("saving native file-sharing credentials");
    }
#endif
    if([[ownership objectForKey:@"RemoteLogin"] boolValue]&&![[journal objectForKey:@"RemoteLoginConfigured"] boolValue]){
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
        int remote=VIMTigerEnableNativeServices(NO,YES);
        if(remote)return remote==75?75:Failure("enabling and confirming native Tiger Remote Login");
#elif defined(VIM_MODERN_ACCOUNT_SETUP)
        // systemsetup requires Full Disk Access on modern macOS, even for
        // root. Configure the requested native launchd service directly;
        // no TCC database edits or extra disk-access authorization is needed.
        if(Run(@"/bin/launchctl",@[@"enable",@"system/com.openssh.sshd"]))return Failure("enabling native Remote Login");
        if(!JobLoaded(CFSTR("com.openssh.sshd"))&&
           Run(@"/bin/launchctl",@[@"bootstrap",@"system",@"/System/Library/LaunchDaemons/ssh.plist"])&&
           !JobLoaded(CFSTR("com.openssh.sshd")))return Failure("loading native Remote Login");
        if(Run(@"/usr/bin/nc",@[@"-z",@"-w",@"2",@"127.0.0.1",@"22"]))return 75;
#else
        if(Run(@"/usr/sbin/systemsetup",[NSArray arrayWithObjects:@"-setremotelogin",@"on",nil])||!JobLoaded(CFSTR("com.openssh.sshd")))return Failure("enabling and confirming Remote Login");
#endif
        [journal setObject:[NSNumber numberWithBool:YES] forKey:@"RemoteLoginConfigured"];
        if(!Save(journal,journalPath))return Failure("saving Remote Login setup");
    }
    if(![[journal objectForKey:@"FirstLoginPolicyPrepared"] boolValue]){
        if([version hasPrefix:@"10.4."]||[version hasPrefix:@"10.5."]){
            // Native Leopard SetupAssistant uses the system AppleSetupDone
            // handoff. Its framework has no Mavericks MiniBuddy/.skipbuddy
            // contract; do not manufacture that per-user policy here.
            [journal setObject:[version hasPrefix:@"10.4."]?@"TigerNativeSetupDone":@"LeopardNativeSetupDone" forKey:@"FirstLoginPolicyMechanism"];
        }else if([version hasPrefix:@"10.7."]||[version hasPrefix:@"10.8."]){
            // Actual 12F2560 MiniLauncher::ShouldLaunchMiniBuddy reads these
            // two MBPerUserState preferences, then calls native transition
            // when cloud setup is already seen. Its Buddy has no .skipbuddy
            // contract. Publish the same state as its FacelessConfiguration,
            // as the verified newly created user, not root's defaults domain.
            NSString *prefs=[[@"/Users/" stringByAppendingString:name] stringByAppendingPathComponent:@"Library/Preferences/com.apple.SetupAssistant.plist"];
            struct stat info;
            if(!lstat([prefs fileSystemRepresentation],&info)){
                if(!S_ISREG(info.st_mode)||info.st_uid!=(uid_t)[uid intValue]||info.st_nlink!=1||(info.st_mode&0022))
                    return Failure("unsafe native first-login preferences");
            }else if(errno!=ENOENT)return Failure("reading native first-login preferences");
            BOOL lion=[version hasPrefix:@"10.7."];
            // Native 11G63 MiniLauncher reads GestureMovieSeen then
            // DidSeeCloudSetup; its own no-trackpad path stores "none".
            // It has no Mountain Lion LastSeenCloudProductVersion contract.
            NSString *key=lion?@"GestureMovieSeen":@"LastSeenCloudProductVersion";
            NSString *value=lion?@"none":version;
            if(Run(@"/usr/bin/sudo",[NSArray arrayWithObjects:@"-H",@"-u",name,@"/usr/bin/defaults",@"write",@"com.apple.SetupAssistant",@"DidSeeCloudSetup",@"-bool",@"true",nil])||
               Run(@"/usr/bin/sudo",[NSArray arrayWithObjects:@"-H",@"-u",name,@"/usr/bin/defaults",@"write",@"com.apple.SetupAssistant",key,@"-string",value,nil]))
                return Failure("preparing native first-login preferences");
            NSDictionary *policy=[NSDictionary dictionaryWithContentsOfFile:prefs];
            if(!policy)return 75; // Native preference publication is still pending.
            if(![[policy objectForKey:@"DidSeeCloudSetup"] isEqual:[NSNumber numberWithBool:YES]]||
               ![[policy objectForKey:key] isEqual:value])
                return Failure("confirming native first-login preferences");
            [journal setObject:lion?@"LionNativeFirstLoginPreferences":@"MountainLionNativeCloudPreferences" forKey:@"FirstLoginPolicyMechanism"];
        }else {
        BOOL intent=[[journal objectForKey:@"FirstLoginCookieIntent"] boolValue];
        int cookie=VIMOpenGLFirstLoginCookie(name,(uid_t)[uid intValue],0);
        if(cookie<0||(cookie&&!intent))return Failure("an unrelated first-login policy already exists");
        [journal setObject:[NSNumber numberWithBool:YES] forKey:@"FirstLoginCookieIntent"];
        if(!Save(journal,journalPath)||VIMOpenGLFirstLoginCookie(name,(uid_t)[uid intValue],1)!=1)
            return Failure("preparing native unattended first login");
        }
        NSString *boot=VIMOpenGLBootSessionUUID(NULL);
        if(!boot)
            return Failure("identifying the first-login boot session");
        [journal setObject:boot forKey:@"FirstLoginPolicyBootSessionUUID"];
        [journal setObject:[NSNumber numberWithBool:YES] forKey:@"FirstLoginPolicyPrepared"];
        if(!Save(journal,journalPath))return Failure("saving native first-login policy");
    }
    errno=0;
    if(!LoginOnce(name,password,journal,journalPath,[version hasPrefix:@"10.13."]))
        return errno==EAGAIN?75:Failure("preparing the first desktop login");
    int marker=open("/var/db/.AppleSetupDone",O_CREAT|O_EXCL|O_WRONLY|O_NOFOLLOW,0644);
    if(marker>=0)close(marker);else if(errno!=EEXIST)return Failure("completing native Setup Assistant policy");
    [journal setObject:@"autologin-armed" forKey:@"Phase"];
    if(!Save(journal,journalPath))return Failure("saving desktop handoff");
    puts("{\"AccountCreated\":true,\"HomeVerified\":true,\"SetupCompleted\":true,\"RestartRequired\":true}");
    close(lock);return 0;
}}
