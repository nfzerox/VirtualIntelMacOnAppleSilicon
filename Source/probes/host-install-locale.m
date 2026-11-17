// Real locale staging into disposable directories; no disk image or VM needed.
#import <Foundation/Foundation.h>
#import "../compat/VIMGuestLocale.h"
#import "../VirtualIntelMac/VIMGuestFamilies.h"
#import "../tools/HostInstall/VIMGuestPath.h"
#include <assert.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#define Require(test,message) do {if(!(test)){fprintf(stderr,"%s\n",[message UTF8String]);abort();}}while(0)
static NSURL *At(NSURL *root,NSString *path){return [root URLByAppendingPathComponent:path];}
static NSDictionary *Plist(NSURL *url){return [NSDictionary dictionaryWithContentsOfURL:url];}
@interface VIMInstallTarget : NSObject
@property NSURL *root;
@property NSMutableDictionary *metadata;
@end
@implementation VIMInstallTarget
- (void)directory:(NSString *)path mode:(mode_t)mode {
    NSError *error=nil;
    Require([NSFileManager.defaultManager createDirectoryAtURL:At(self.root,path) withIntermediateDirectories:YES
        attributes:@{NSFilePosixPermissions:@(mode)} error:&error],error.localizedDescription);
}
- (void)file:(NSString *)path data:(NSData *)data mode:(mode_t)mode {
    Require([data writeToURL:At(self.root,path) atomically:YES],@"fixture write failed");
    Require(!chmod(At(self.root,path).fileSystemRepresentation,mode),@"fixture chmod failed");
    self.metadata[path]=@[@0,@0,@(S_IFREG|mode)];
}
- (void)plist:(NSString *)path value:(NSDictionary *)value mode:(mode_t)mode {
    [self directory:path.stringByDeletingLastPathComponent mode:0755];
    NSData *data=[NSPropertyListSerialization dataWithPropertyList:value format:NSPropertyListXMLFormat_v1_0 options:0 error:nil];
    [self file:path data:data mode:mode];
}
@end
#include "../tools/HostInstall/VIMGuestLocaleStage.h"

int main(void){@autoreleasepool {
    NSDictionary *before=[NSUserDefaults.standardUserDefaults persistentDomainForName:NSGlobalDomain];
    NSDictionary *snapshot=VIMCaptureGuestLocale();
    assert([snapshot[@"Preferences"] count]&&[snapshot[@"TimeZoneData"] length]>=4);
    assert([NSPropertyListSerialization propertyList:snapshot isValidForFormat:NSPropertyListXMLFormat_v1_0]);
    assert([before isEqual:[NSUserDefaults.standardUserDefaults persistentDomainForName:NSGlobalDomain]]);
    NSData *zone=[NSTimeZone timeZoneWithName:@"Europe/Paris"].data;
    NSDictionary *profile=@{@"Preferences":@{@"AppleLanguages":@[@"fr-FR",@"de",@"en"],@"AppleLocale":@"fr_FR",
        @"AppleMetricUnits":@YES,@"AppleICUForce24HourTime":@YES,@"UnrelatedHostPreference":@"not copied"},
        @"TimeZone":@"Europe/Paris",@"TimeZoneData":zone};
    NSDictionary *preferences=VIMGuestLocalePreferences(profile);
    assert(!preferences[@"UnrelatedHostPreference"]);
    assert([VIMGuestLocaleTemplate(@[@"English",@"French",@"German"],profile) isEqual:@"French"]);
    assert([VIMGuestLocaleTemplate(@[@"English"],profile) isEqual:@"English"]);
    assert([VIMGuestLocalePreferences(nil)[@"AppleLocale"] isEqual:@"en_US"]);
    for(NSString *guest in @[@"tiger",@"mavericks",@"catalina",@"bigsur",@"tahoe"]){
        NSURL *root=[NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:
            [@"VIM-LocaleFixture-" stringByAppendingString:NSUUID.UUID.UUIDString]]];
        VIMInstallTarget *target=[VIMInstallTarget new];target.root=root;target.metadata=[NSMutableDictionary dictionary];
        BOOL split=VIMIsAssistantGuest(guest);
        NSString *directory=split?@"private/var/db/timezone/tz/test/zoneinfo":@"usr/share/zoneinfo";
        [target directory:[directory stringByAppendingPathComponent:@"Europe"] mode:0755];
        if(split){
            NSURL *alias=At(root,@"private/var/db/timezone/zoneinfo");
            assert(!symlink("/var/db/timezone/tz/test/zoneinfo",alias.fileSystemRepresentation));
        }
        [target file:[directory stringByAppendingPathComponent:@"Europe/Paris"] data:[@"old" dataUsingEncoding:NSUTF8StringEncoding] mode:0644];
        NSURL *parent=At(root,[directory stringByAppendingPathComponent:@"Europe"]);
        assert(!chmod(parent.fileSystemRepresentation,0555));
        [target plist:@"Library/Preferences/.GlobalPreferences.plist" value:@{@"Preserved":@YES} mode:0644];
        StageHostLocale(target,root,@{@"Guest":guest,@"HostLocale":profile,@"Enabled":@NO});
        NSDictionary *written=Plist(At(root,@"Library/Preferences/.GlobalPreferences.plist"));
        assert([written[@"AppleLocale"] isEqual:@"fr_FR"]&&[written[@"Preserved"] boolValue]);
        assert([written[@"AppleLanguages"] isEqual:preferences[@"AppleLanguages"]]);
        assert([[NSData dataWithContentsOfURL:At(root,[directory stringByAppendingPathComponent:@"Europe/Paris"])] isEqual:zone]);
        struct stat info;assert(!lstat(parent.fileSystemRepresentation,&info)&&(info.st_mode&0777)==0555);
        char link[1024];ssize_t n=readlink(At(root,@"private/etc/localtime").fileSystemRepresentation,link,sizeof(link)-1);
        assert(n>0);link[n]=0;
        assert(!strcmp(link,split?"/var/db/timezone/zoneinfo/Europe/Paris":"/usr/share/zoneinfo/Europe/Paris"));
        assert(![NSFileManager.defaultManager fileExistsAtPath:At(root,@"Users").path]);
        assert(!chmod(parent.fileSystemRepresentation,0755));
        Require([NSFileManager.defaultManager removeItemAtURL:root error:nil],@"fixture cleanup failed");
    }
    for(NSString *guest in @[@"bigsur",@"tahoe"]) {
        NSURL *root=[NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:
            [@"VIM-LocaleSystemFixture-" stringByAppendingString:NSUUID.UUID.UUIDString]]];
        NSURL *system=At(root,@"System"),*data=At(root,@"Data");
        VIMInstallTarget *original=[VIMInstallTarget new];original.root=system;original.metadata=[NSMutableDictionary dictionary];
        [original directory:@"usr/share/zoneinfo.default/Europe" mode:0755];
        [original file:@"usr/share/zoneinfo.default/Europe/Paris" data:zone mode:0444];
        NSURL *parent=At(system,@"usr/share/zoneinfo.default/Europe"),*file=At(parent,@"Paris");
        assert(!chmod(parent.fileSystemRepresentation,0555));
        struct stat beforeZone,afterZone;assert(!lstat(file.fileSystemRepresentation,&beforeZone));
        VIMInstallTarget *target=[VIMInstallTarget new];target.root=data;target.metadata=[NSMutableDictionary dictionary];
        [target directory:@"private/var/db/timezone" mode:0755];
        NSURL *database=At(data,@"private/var/db/timezone"),*alias=At(database,@"zoneinfo");
        assert(!symlink("/usr/share/zoneinfo.default",alias.fileSystemRepresentation));
        assert(!chmod(database.fileSystemRepresentation,0555));
        StageHostLocale(target,system,@{@"Guest":guest,@"HostLocale":profile,@"Enabled":@NO});
        char link[1024];ssize_t n=readlink(At(data,@"private/etc/localtime").fileSystemRepresentation,link,sizeof(link)-1);
        assert(n>0);link[n]=0;assert(!strcmp(link,"/var/db/timezone/zoneinfo/Europe/Paris"));
        n=readlink(alias.fileSystemRepresentation,link,sizeof(link)-1);
        assert(n>0);link[n]=0;assert(!strcmp(link,"/usr/share/zoneinfo.default"));
        assert(!lstat(file.fileSystemRepresentation,&afterZone));
        assert(afterZone.st_ino==beforeZone.st_ino&&afterZone.st_mode==beforeZone.st_mode&&
            afterZone.st_mtimespec.tv_sec==beforeZone.st_mtimespec.tv_sec&&afterZone.st_mtimespec.tv_nsec==beforeZone.st_mtimespec.tv_nsec&&
            afterZone.st_ctimespec.tv_sec==beforeZone.st_ctimespec.tv_sec&&afterZone.st_ctimespec.tv_nsec==beforeZone.st_ctimespec.tv_nsec);
        assert([[NSData dataWithContentsOfURL:file] isEqual:zone]);
        assert(![NSFileManager.defaultManager fileExistsAtPath:At(data,@"usr").path]);
        assert(![NSFileManager.defaultManager fileExistsAtPath:At(data,@"Users").path]);
        assert(!chmod(parent.fileSystemRepresentation,0755));assert(!chmod(database.fileSystemRepresentation,0755));
        Require([NSFileManager.defaultManager removeItemAtURL:root error:nil],@"split fixture cleanup failed");
    }
    puts("PASS host snapshot, formats, localized templates, seven offline layouts, readonly modes, sealed-System isolation and manual setup");
    return 0;
}}
