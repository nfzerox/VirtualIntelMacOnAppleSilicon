#import "VIMAutomaticSetup.h"
#import "VIMMachine.h"
#import <Security/Security.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
static BOOL SetupFailure(NSError **error,NSString *message) {
    if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.AutomaticSetup" code:1 userInfo:@{NSLocalizedDescriptionKey:message}];return NO;
}
static NSDictionary *Credential(NSString *identifier) {
    if(![identifier isKindOfClass:NSString.class]||![[NSUUID alloc] initWithUUIDString:identifier])return nil;
    return @{(__bridge id)kSecClass:(__bridge id)kSecClassGenericPassword,
        (__bridge id)kSecAttrService:@"com.mac.virtualintelmac.automatic-setup",
        (__bridge id)kSecAttrAccount:identifier};
}
static int CredentialDirectory(BOOL create,NSError **error) {
    NSURL *support=[NSFileManager.defaultManager URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask].firstObject;
    NSURL *directory=[support URLByAppendingPathComponent:@"Virtual Intel Mac/Setup Credentials" isDirectory:YES];
    if(!directory){SetupFailure(error,@"The private setup-password folder is unavailable.");return -1;}
    if(create&&![NSFileManager.defaultManager createDirectoryAtURL:directory withIntermediateDirectories:YES attributes:@{NSFilePosixPermissions:@0700} error:error])return -1;
    int fd=open(directory.fileSystemRepresentation,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0){if(create||errno!=ENOENT)SetupFailure(error,@"The private setup-password folder could not be opened.");return -1;}
    struct stat st;
    if(fstat(fd,&st)||!S_ISDIR(st.st_mode)||st.st_uid!=getuid()||(st.st_mode&0077)){
        close(fd);SetupFailure(error,@"The setup-password folder must be private to your Mac account.");return -1;
    }
    if(create)[directory setResourceValue:@YES forKey:NSURLIsExcludedFromBackupKey error:NULL];
    return fd;
}
static BOOL PrivateCredential(struct stat *st) {return S_ISREG(st->st_mode)&&st->st_uid==getuid()&&st->st_nlink==1&&!(st->st_mode&0077)&&st->st_size<=511;}
@implementation VIMAutomaticSetup
+ (BOOL)supportsGuest:(NSString *)guest {return [@[@"tiger",@"leopard",@"snowleopard",@"lion",@"mountainlion",@"mavericks",@"yosemite",@"elcapitan",@"sierra",@"highsierra",@"mojave",@"catalina",@"bigsur"] containsObject:guest];}
+ (NSDictionary *)installerConfigurationForVolumeUUID:(NSString *)UUID error:(NSError **)error {
    if(![UUID isKindOfClass:NSString.class]||![[NSUUID alloc] initWithUUIDString:UUID]||
       [UUID isEqual:@"00000000-0000-0000-0000-000000000000"]){SetupFailure(error,@"The new Macintosh HD could not be identified safely.");return nil;}
    return @{@"Package":@"/System/Installation/Packages/OSInstall.mpkg",@"InstallType":@"automated",
        @"Language":@"English",@"TargetUUID":UUID,@"ShouldErase":@NO,@"ShouldArchiveAndInstall":@NO,@"QuitWhenDone":@YES};
}
+ (NSString *)defaultUsername {
    NSString *name=NSUserName().lowercaseString;
    return [self validateUsername:name error:NULL]?name:@"user";
}
+ (BOOL)validateUsername:(NSString *)username error:(NSError **)error {
    NSRegularExpression *pattern=[NSRegularExpression regularExpressionWithPattern:@"^[a-z_][a-z0-9_-]{0,30}$" options:0 error:NULL];
    if(![username isKindOfClass:NSString.class]||[pattern numberOfMatchesInString:username options:0 range:NSMakeRange(0,username.length)]!=1||
       [username hasPrefix:@"_"]||[@[@"root",@"daemon",@"nobody",@"guest",@"wheel"] containsObject:username])
        return SetupFailure(error,@"Use an account name of 1–31 lowercase letters, numbers, hyphens or underscores, starting with a letter.");
    return YES;
}
+ (BOOL)validatePassword:(NSString *)password error:(NSError **)error {
    if(![password isKindOfClass:NSString.class]||[password lengthOfBytesUsingEncoding:NSUTF8StringEncoding]>511||
       [password rangeOfString:[NSString stringWithFormat:@"%C",(unichar)0]].location!=NSNotFound)
        return SetupFailure(error,@"Choose a password of at most 511 UTF-8 bytes, without a null character.");
    return YES;
}
+ (BOOL)savePassword:(NSString *)password identifier:(NSString *)identifier error:(NSError **)error {
    if(![self validatePassword:password error:error])return NO;
    if(!Credential(identifier))return SetupFailure(error,@"This setup session has no valid credential identifier.");
    int directory=CredentialDirectory(YES,error);if(directory<0)return NO;
    struct stat st;BOOL exists=fstatat(directory,identifier.UTF8String,&st,AT_SYMLINK_NOFOLLOW)==0;
    if((exists&&!PrivateCredential(&st))||(!exists&&errno!=ENOENT)){close(directory);return SetupFailure(error,@"The saved setup password has unexpected permissions. It was not replaced.");}
    NSString *temporary=[@".pending-" stringByAppendingString:NSUUID.UUID.UUIDString];
    int fd=openat(directory,temporary.UTF8String,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    NSData *data=[password dataUsingEncoding:NSUTF8StringEncoding];BOOL success=fd>=0&&fchmod(fd,0600)==0;
    const unsigned char *bytes=data.bytes;NSUInteger remaining=data.length;
    while(success&&remaining){ssize_t count=write(fd,bytes,remaining);if(count<0&&errno==EINTR)continue;
        if(count<=0){success=NO;break;}bytes+=count;remaining-=(NSUInteger)count;}
    if(success)success=fsync(fd)==0;if(fd>=0)close(fd);
    if(success)success=renameat(directory,temporary.UTF8String,directory,identifier.UTF8String)==0;
    if(!success)unlinkat(directory,temporary.UTF8String,0);else fsync(directory);
    close(directory);return success||SetupFailure(error,@"The temporary setup password could not be saved privately on this Mac. Try again.");
}
+ (NSString *)passwordForIdentifier:(NSString *)identifier error:(NSError **)error {
    NSDictionary *identity=Credential(identifier);if(!identity){SetupFailure(error,@"This setup session has no valid credential identifier.");return nil;}
    NSError *failure=nil;int directory=CredentialDirectory(NO,&failure);
    if(failure){if(error)*error=failure;return nil;}
    if(directory>=0){
        int fd=openat(directory,identifier.UTF8String,O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC);int openError=errno;close(directory);
        if(fd>=0){struct stat st;BOOL safe=fstat(fd,&st)==0&&PrivateCredential(&st);NSMutableData *data=[NSMutableData new];unsigned char bytes[512];
            while(safe){ssize_t count=read(fd,bytes,sizeof(bytes));if(count<0&&errno==EINTR)continue;if(count==0)break;
                if(count<0||data.length+(NSUInteger)count>511){safe=NO;break;}[data appendBytes:bytes length:(NSUInteger)count];}
            close(fd);NSString *password=safe?[[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding]:nil;
            if(password&&[self validatePassword:password error:error])return password;
            SetupFailure(error,@"The saved setup password is not a valid private credential. Enter it again.");return nil;
        }
        if(openError!=ENOENT){SetupFailure(error,@"The saved setup password could not be read safely. Enter it again.");return nil;}
    }
    // Compatibility only: migrate a previously approved Keychain item without
    // ever presenting an authorization dialog. New setups do not use Keychain.
    NSMutableDictionary *query=[identity mutableCopy];query[(__bridge id)kSecReturnData]=@YES;query[(__bridge id)kSecMatchLimit]=(__bridge id)kSecMatchLimitOne;
    query[(__bridge id)kSecUseAuthenticationUI]=(__bridge id)kSecUseAuthenticationUIFail;
    CFTypeRef result=NULL;OSStatus status=SecItemCopyMatching((__bridge CFDictionaryRef)query,&result);
    if(status!=errSecSuccess){SetupFailure(error,@"Enter the virtual Mac’s setup password again. Its saved credential is not available on this Mac.");return nil;}
    NSData *data=CFBridgingRelease(result);NSString *password=[[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
    if(!password||![self savePassword:password identifier:identifier error:error])return nil;
    NSMutableDictionary *cleanup=[identity mutableCopy];cleanup[(__bridge id)kSecUseAuthenticationUI]=(__bridge id)kSecUseAuthenticationUIFail;
    SecItemDelete((__bridge CFDictionaryRef)cleanup);return password;
}
+ (BOOL)removePasswordForIdentifier:(NSString *)identifier error:(NSError **)error {
    NSDictionary *query=Credential(identifier);if(!query)return SetupFailure(error,@"This setup session has no valid credential identifier.");
    NSError *failure=nil;int directory=CredentialDirectory(NO,&failure);if(failure){if(error)*error=failure;return NO;}
    if(directory>=0){struct stat st;int found=fstatat(directory,identifier.UTF8String,&st,AT_SYMLINK_NOFOLLOW);
        BOOL safe=found==0?PrivateCredential(&st):errno==ENOENT;
        BOOL removed=safe&&(found!=0||unlinkat(directory,identifier.UTF8String,0)==0);close(directory);
        if(!removed)return SetupFailure(error,@"The temporary setup password could not be retired safely. Its record was retained.");}
    NSMutableDictionary *cleanup=[query mutableCopy];cleanup[(__bridge id)kSecUseAuthenticationUI]=(__bridge id)kSecUseAuthenticationUIFail;
    SecItemDelete((__bridge CFDictionaryRef)cleanup);return YES;
}
+ (NSURL *)configurationMediaForMachine:(VIMMachine *)machine error:(NSError **)error {
    if(!machine.preMetalGuest||!machine.configuration[@"InstallerMedia"])return nil;
    NSURL *state=[machine.URL URLByAppendingPathComponent:@"State"];
    NSDictionary *target=[NSDictionary dictionaryWithContentsOfURL:[state URLByAppendingPathComponent:@"InstallationTarget.plist"]];
    if(!target)return nil; // Imported development machines have no new disk claim.
    if(![self installerConfigurationForVolumeUUID:target[@"VolumeUUID"] error:error])return nil;
    NSDictionary *profile=[NSDictionary dictionaryWithContentsOfURL:[state URLByAppendingPathComponent:@"AutomaticSetup.plist"]];
    NSMutableDictionary *policy=[@{@"FormatVersion":@1,@"Kind":@"VirtualIntelMacLegacySetup",@"Guest":machine.guest,
        @"MachineIdentifier":machine.configuration[@"Identifier"],@"VolumeUUID":target[@"VolumeUUID"],@"Enabled":@NO} mutableCopy];
    if(profile) {
        if(![profile[@"MachineIdentifier"] isEqual:machine.configuration[@"Identifier"]]||
           ![profile[@"VolumeUUID"] isEqual:target[@"VolumeUUID"]]){SetupFailure(error,@"The saved automatic setup target does not match this Mac.");return nil;}
        if(![self validateUsername:profile[@"Username"] error:error])return nil;
        for(NSString *key in @[@"Enabled",@"Username",@"FileSharing",@"RemoteLogin",@"AutomaticLogin"])if(profile[key])policy[key]=profile[key];
    }
    NSURL *image=[state URLByAppendingPathComponent:@"LegacySetup.iso"],*receipt=[state URLByAppendingPathComponent:@"LegacySetupMedia.plist"];
    struct stat st;
    if([[NSDictionary dictionaryWithContentsOfURL:receipt] isEqual:policy]&&
       !lstat(image.fileSystemRepresentation,&st)&&S_ISREG(st.st_mode)&&st.st_size>0)return image;
    NSFileManager *fm=NSFileManager.defaultManager;
    NSURL *job=[state URLByAppendingPathComponent:[@".SetupMedia-" stringByAppendingString:NSUUID.UUID.UUIDString]];
    // This disc is attached to the guest as an ATAPI CD-ROM (ide-cd), which
    // expects ISO9660 structure to consider the medium readable at all; an
    // HFS-only image (no -iso) mounts fine when opened directly on the host
    // but is reported as unreadable by the guest. With -iso, makehybrid always
    // appends .iso to the given base name itself (even one that already ends
    // in .dmg), rather than reusing that exact -o path as HFS-only mode does;
    // pass an extensionless base and reference the actual <base>.iso result.
    NSURL *content=[job URLByAppendingPathComponent:@"Content"],*base=[job URLByAppendingPathComponent:@"Setup"],
        *output=[job URLByAppendingPathComponent:@"Setup.iso"];
    if(![fm createDirectoryAtURL:content withIntermediateDirectories:YES attributes:@{NSFilePosixPermissions:@0700} error:error])return nil;
    if(![policy writeToURL:[content URLByAppendingPathComponent:@"VirtualIntelMac.plist"] error:error])return nil;
    NSURL *log=[job URLByAppendingPathComponent:@"Media.log"];
    if(![fm createFileAtPath:log.path contents:nil attributes:@{NSFilePosixPermissions:@0600}]){SetupFailure(error,@"The automatic setup disc could not be prepared.");return nil;}
    NSFileHandle *handle=[NSFileHandle fileHandleForWritingToURL:log error:error];if(!handle)return nil;
    NSTask *task=[NSTask new];task.executableURL=[NSURL fileURLWithPath:@"/usr/bin/hdiutil"];
    task.arguments=@[@"makehybrid",@"-hfs",@"-iso",@"-hfs-volume-name",@"VIM Setup",@"-o",base.path,content.path];
    task.standardInput=NSFileHandle.fileHandleWithNullDevice;task.standardOutput=handle;task.standardError=handle;
    BOOL launched=[task launchAndReturnError:error];if(launched)[task waitUntilExit];[handle closeFile];
    if(!launched||task.terminationStatus){SetupFailure(error,@"The automatic setup disc could not be prepared. Its diagnostic files were retained in this Mac’s State folder.");return nil;}
    // Only this stopped VM's generated file is replaced. Never modify a
    // reusable installer, a mounted disc or any user's selected media.
    if(rename(output.fileSystemRepresentation,image.fileSystemRepresentation)||![policy writeToURL:receipt error:error]) {
        SetupFailure(error,@"The automatic setup disc could not be saved.");return nil;
    }
    [fm removeItemAtURL:job error:NULL];return image;
}
@end
