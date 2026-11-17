// Included after VIMInstallTarget. Only writes the new, offline guest volume.
#include <limits.h>
static void StageLocaleTimeZoneFile(VIMInstallTarget *target,NSString *relative,NSData *data) {
    NSMutableArray *opened=[NSMutableArray array];
    @try {
        NSString *directory=@"";
        for(NSString *part in relative.stringByDeletingLastPathComponent.pathComponents) {
            directory=[directory stringByAppendingPathComponent:part];
            [target directory:directory mode:0755];
            NSURL *url=At(target.root,directory);struct stat info;
            Require(!lstat(url.fileSystemRepresentation,&info)&&S_ISDIR(info.st_mode),@"Could not prepare the guest’s time zone folder.");
            if(!(info.st_mode&S_IWUSR)) {
                Require(!chmod(url.fileSystemRepresentation,(info.st_mode&07777)|S_IWUSR),@"Could not open the guest’s time zone folder.");
                [opened addObject:@[url,@(info.st_mode&07777)]];
            }
        }
        [target file:relative data:data mode:0644];
    } @finally {
        for(NSArray *entry in opened.reverseObjectEnumerator)
            Require(!chmod([entry[0] fileSystemRepresentation],[entry[1] unsignedShortValue]),@"Could not restore the guest’s time zone folder permissions.");
    }
}
static void StageHostLocale(VIMInstallTarget *target,NSURL *system,NSDictionary *owner) {
    NSDictionary *profile=owner[@"HostLocale"];
    if(![profile isKindOfClass:NSDictionary.class]||!profile.count)return;
    NSString *global=@"Library/Preferences/.GlobalPreferences.plist";
    NSMutableDictionary *preferences=[Plist(At(target.root,global)) mutableCopy]?:[NSMutableDictionary dictionary];
    [preferences addEntriesFromDictionary:VIMGuestLocalePreferences(profile)];
    [target plist:global value:preferences mode:0644];

    NSString *name=profile[@"TimeZone"];NSData *data=profile[@"TimeZoneData"];
    if(![name isKindOfClass:NSString.class]||!name.length||name.isAbsolutePath||
       [name.pathComponents containsObject:@".."]||![NSTimeZone timeZoneWithName:name]||
       ![data isKindOfClass:NSData.class]||data.length<4||memcmp(data.bytes,"TZif",4))return;
    BOOL split=VIMIsAssistantGuest(owner[@"Guest"]);
    NSString *base=split?@"private/var/db/timezone/zoneinfo":@"usr/share/zoneinfo";
    [target directory:base.stringByDeletingLastPathComponent mode:0755];
    NSURL *directory=VIMResolveGuestPath(target.root,base,YES,nil);
    BOOL systemZone=NO;
    if(!directory) {
        // A split Data volume has no System /var alias. Resolve this ordinary
        // timezone alias within Data, never against the host filesystem.
        NSURL *link=VIMResolveGuestPath(target.root,base,NO,nil);struct stat info;
        if(link&&!lstat(link.fileSystemRepresentation,&info)&&S_ISLNK(info.st_mode)) {
            char bytes[PATH_MAX+1];ssize_t size=readlink(link.fileSystemRepresentation,bytes,PATH_MAX);
            if(size>0&&size<PATH_MAX) {
                bytes[size]=0;NSString *destination=@(bytes);
                NSString *path=[destination hasPrefix:@"/var/"]?[@"private" stringByAppendingString:destination]:
                    destination.isAbsolutePath?[destination substringFromIndex:1]:[base.stringByDeletingLastPathComponent stringByAppendingPathComponent:destination];
                directory=VIMResolveGuestPath(target.root,path,YES,nil);
                if(!directory&&split&&system) {
                    // Big Sur's original alias points to /usr/share/zoneinfo.default
                    // on System, not Data. Select its existing zone without writing
                    // host bytes into the sealed volume or replacing the alias.
                    NSURL *zone=VIMResolveGuestPath(system,[path stringByAppendingPathComponent:name],YES,nil);
                    NSData *original=zone?[NSData dataWithContentsOfURL:zone options:NSDataReadingMappedIfSafe error:nil]:nil;
                    systemZone=original.length>=4&&!memcmp(original.bytes,"TZif",4);
                }
            }
        } else if(link&&errno==ENOENT) {
            [target directory:base mode:0755];directory=VIMResolveGuestPath(target.root,base,YES,nil);
        }
    }
    if(!directory&&!systemZone) {
        // Unrecognized optional timezone layouts must not break an otherwise
        // installable OS. Language/region defaults above are still prepared.
        fprintf(stderr,"The guest’s timezone database was unavailable; its original time zone was kept.\n");return;
    }
    if(directory) {
        NSString *prefix=[target.root.path stringByAppendingString:@"/"];
        Require([directory.path hasPrefix:prefix],@"The guest’s time zone folder is outside its volume.");
        NSString *zone=[[directory.path substringFromIndex:prefix.length] stringByAppendingPathComponent:name];
        NSURL *resolved=VIMResolveGuestPath(target.root,zone,YES,nil)?:VIMResolveGuestPath(target.root,zone,NO,nil);
        if(!resolved)return;
        StageLocaleTimeZoneFile(target,[resolved.path substringFromIndex:prefix.length],data);
    }
    NSString *relative=@"private/etc/localtime";
    [target directory:relative.stringByDeletingLastPathComponent mode:0755];
    NSURL *localtime=VIMResolveGuestPath(target.root,relative,NO,nil);
    Require(localtime!=nil,@"Could not locate the guest’s time zone setting.");
    Require(!unlink(localtime.fileSystemRepresentation)||errno==ENOENT,@"Could not replace the guest’s original time zone setting.");
    NSString *destination=[(split?@"/var/db/timezone/zoneinfo":@"/usr/share/zoneinfo") stringByAppendingPathComponent:name];
    Require(!symlink(destination.fileSystemRepresentation,localtime.fileSystemRepresentation),@"Could not save the guest’s time zone setting.");
    target.metadata[relative]=@[@0,@0,@(S_IFLNK|0777)];
}
