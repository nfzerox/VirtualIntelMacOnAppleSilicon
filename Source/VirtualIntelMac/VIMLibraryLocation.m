#import "VIMLibraryLocation.h"
#import "VIMAlerts.h"
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

static NSString *const LayoutMarker=@".VirtualIntelMacLibrary.plist";
static BOOL Layout2(NSURL *root) {
    return [[[NSDictionary dictionaryWithContentsOfURL:[root URLByAppendingPathComponent:LayoutMarker]] objectForKey:@"FormatVersion"] isEqual:@2];
}
static BOOL LocationError(NSError **error,NSInteger code,NSString *message) {
    if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.Library" code:code userInfo:@{NSLocalizedDescriptionKey:message}];return NO;
}
static BOOL WriteLocationPlist(id object,NSURL *url,NSError **error) {
    NSData *data=[NSPropertyListSerialization dataWithPropertyList:object format:NSPropertyListBinaryFormat_v1_0 options:0 error:error];
    return data&&[data writeToURL:url options:NSDataWritingAtomic error:error];
}
static id RelocatedValue(id value,NSDictionary<NSString *,NSString *> *moves) {
    if([value isKindOfClass:NSString.class]) {
        NSString *best=nil;
        for(NSString *old in moves)if((!best||old.length>best.length)&&([value isEqual:old]||[value hasPrefix:[old stringByAppendingString:@"/"]]))best=old;
        if(best)return [moves[best] stringByAppendingString:[value substringFromIndex:best.length]];
    } else if([value isKindOfClass:NSArray.class]) {
        NSMutableArray *result=[NSMutableArray new];for(id item in value)[result addObject:RelocatedValue(item,moves)];return result;
    } else if([value isKindOfClass:NSDictionary.class]) {
        NSMutableDictionary *result=[NSMutableDictionary new];for(id key in value)result[RelocatedValue(key,moves)]=RelocatedValue(value[key],moves);return result;
    }
    return value;
}
static BOOL PrepareRoot(NSURL *root,NSURL *legacy,NSError **error) {
    if(Layout2(root))return YES;
    NSFileManager *fm=NSFileManager.defaultManager;
    if(![fm createDirectoryAtURL:root withIntermediateDirectories:YES attributes:nil error:error])return NO;
    int lock=open([root URLByAppendingPathComponent:@".LibraryLayout.lock"].fileSystemRepresentation,O_CREAT|O_RDWR|O_NOFOLLOW|O_CLOEXEC,0600);
    struct stat info;
    if(lock<0||fstat(lock,&info)||!S_ISREG(info.st_mode)||info.st_nlink!=1||flock(lock,LOCK_EX|LOCK_NB)) {
        if(lock>=0)close(lock);return LocationError(error,EBUSY,@"Another copy of Virtual Intel Mac is organizing this library. Try again after it finishes.");
    }
    NSMutableArray<NSNumber *> *locks=[NSMutableArray new];
    BOOL (^finish)(BOOL)=^BOOL(BOOL result){for(NSNumber *fd in locks)close(fd.intValue);close(lock);return result;};
    if(Layout2(root))return finish(YES);
    // Never relocate an active creation job, its resumed transfers, or any
    // machine held by another app process. Locks are retained through publish.
    NSDictionary *job=[NSUserDefaults.standardUserDefaults dictionaryForKey:@"VIMCreationJob"];
    NSString *jobLibrary=job[@"Library"];
    if(jobLibrary.length&&[[VIMLibraryLocation rootURLForLibrary:[NSURL fileURLWithPath:jobLibrary]].path isEqual:root.path]&&[@[@"downloading",@"preparing",@"creating",@"stopping"] containsObject:job[@"Phase"]])
        return finish(LocationError(error,EBUSY,@"The library will be organized after the current Mac finishes being created."));
    NSURL *machines=[root URLByAppendingPathComponent:@"Machines" isDirectory:YES];
    NSURL *journal=[root URLByAppendingPathComponent:@".LibraryLayoutMigration.plist"];
    NSDictionary *record=[NSDictionary dictionaryWithContentsOfURL:journal];
    if(record&&(![record[@"FormatVersion"] isEqual:@1]||![record[@"Legacy"] isEqual:legacy.path]||![record[@"Moves"] isKindOfClass:NSDictionary.class]))
        return finish(LocationError(error,1,@"The library’s layout record is unreadable. Its files have not been removed."));
    NSMutableDictionary *moves=record?[[record objectForKey:@"Moves"] mutableCopy]:[NSMutableDictionary new];
    if(!moves)return finish(LocationError(error,1,@"The library’s layout record is unreadable. Its files have not been removed."));
    if(!record) {
        for(NSURL *item in [fm contentsOfDirectoryAtURL:legacy includingPropertiesForKeys:nil options:0 error:nil]?:@[])
            if([item.pathExtension isEqual:@"vintelmac"]&&![item.URLByDeletingLastPathComponent.path isEqual:machines.path])moves[item.path]=[machines URLByAppendingPathComponent:item.lastPathComponent].path;
        NSURL *downloads=[legacy URLByAppendingPathComponent:@"Downloads"],*preparation=[downloads URLByAppendingPathComponent:@"Preparation"];
        if([fm fileExistsAtPath:preparation.path])moves[preparation.path]=[root URLByAppendingPathComponent:@"Installers"].path;
        if(![downloads.path isEqual:[root URLByAppendingPathComponent:@"Downloads"].path]&&[fm fileExistsAtPath:downloads.path])moves[downloads.path]=[root URLByAppendingPathComponent:@"Downloads"].path;
    }
    // Longest prefix first is not sufficient for physical moves: move nested
    // Preparation before its old Downloads parent. Persist the exact plan.
    for(id old in moves)if(![old isKindOfClass:NSString.class]||![moves[old] isKindOfClass:NSString.class])return finish(LocationError(error,1,@"The library’s layout record is invalid."));
    NSArray *order=[moves.allKeys sortedArrayUsingComparator:^NSComparisonResult(NSString *a,NSString *b){return a.length>b.length?NSOrderedAscending:(a.length<b.length?NSOrderedDescending:[a compare:b]);}];
    NSMutableSet *packages=[NSMutableSet new];
    for(NSString *old in order) {
        NSString *new=moves[old];
        if(![old isAbsolutePath]||![new isAbsolutePath]||![old hasPrefix:[legacy.path stringByAppendingString:@"/"]]||![new hasPrefix:[root.path stringByAppendingString:@"/"]])
            return finish(LocationError(error,1,@"The library’s layout record contains an unexpected location."));
        BOOL original=[fm fileExistsAtPath:old],destination=[fm fileExistsAtPath:new];
        if(original&&destination)return finish(LocationError(error,1,@"Two library items have the same name. Nothing will be overwritten. Rename the duplicate and try again."));
        if(!original&&!destination)return finish(LocationError(error,1,@"A library item is missing. Reconnect its drive before trying again."));
        if([old.pathExtension isEqual:@"vintelmac"])[packages addObject:original?old:new];
    }
    for(NSURL *item in [fm contentsOfDirectoryAtURL:machines includingPropertiesForKeys:nil options:0 error:nil]?:@[])
        if([item.pathExtension isEqual:@"vintelmac"])[packages addObject:item.path];
    for(NSString *package in packages) {
            int fd=open([[package stringByAppendingPathComponent:@"State"] stringByAppendingPathComponent:@"Session.lock"].fileSystemRepresentation,O_CREAT|O_RDWR|O_NOFOLLOW|O_CLOEXEC,0600);
            if(fd<0||fstat(fd,&info)||!S_ISREG(info.st_mode)||info.st_nlink!=1||flock(fd,LOCK_EX|LOCK_NB)) {
                if(fd>=0)close(fd);return finish(LocationError(error,EBUSY,@"The library will be organized after all its virtual Macs have shut down."));
            }
            [locks addObject:@(fd)];
            fd=open([package stringByAppendingPathComponent:@"State/Configuration.lock"].fileSystemRepresentation,O_CREAT|O_RDWR|O_NOFOLLOW|O_CLOEXEC,0600);
            if(fd<0||fstat(fd,&info)||!S_ISREG(info.st_mode)||info.st_nlink!=1||flock(fd,LOCK_EX|LOCK_NB)){if(fd>=0)close(fd);return finish(LocationError(error,EBUSY,@"Wait for this Mac’s settings to finish saving, then try again."));}
            [locks addObject:@(fd)];
    }
    if(!WriteLocationPlist(@{@"FormatVersion":@1,@"Legacy":legacy.path,@"Moves":moves},journal,error))return finish(NO);
    if(![fm createDirectoryAtURL:machines withIntermediateDirectories:YES attributes:nil error:error])return finish(NO);
    for(NSString *old in order)if([fm fileExistsAtPath:old]&&![fm moveItemAtPath:old toPath:moves[old] error:error])return finish(NO);
    for(NSString *folder in @[@"Downloads",@"Installers"])
        if(![fm createDirectoryAtURL:[root URLByAppendingPathComponent:folder] withIntermediateDirectories:YES attributes:nil error:error])return finish(NO);
    // Only property lists belonging to a machine are rewritten. Guest disks,
    // firmware, downloaded package bytes and image contents are never touched.
    for(NSURL *package in [fm contentsOfDirectoryAtURL:machines includingPropertiesForKeys:nil options:0 error:error]?:@[]) {
        if(![package.pathExtension isEqual:@"vintelmac"])continue;
        NSMutableArray *plists=[NSMutableArray arrayWithObject:[package URLByAppendingPathComponent:@"Machine.plist"]];
        for(NSString *subdir in @[@"Installation",@"State"])
            for(NSURL *file in [fm enumeratorAtURL:[package URLByAppendingPathComponent:subdir] includingPropertiesForKeys:nil options:NSDirectoryEnumerationSkipsHiddenFiles errorHandler:nil])
                if([file.pathExtension isEqual:@"plist"])[plists addObject:file];
        for(NSURL *file in plists) {
            NSData *data=[NSData dataWithContentsOfURL:file options:0 error:nil];
            id value=data?[NSPropertyListSerialization propertyListWithData:data options:0 format:NULL error:nil]:nil;
            if(!value)continue;
            id relocated=RelocatedValue(value,moves);
            if(![value isEqual:relocated]&&!WriteLocationPlist(relocated,file,error))return finish(NO);
        }
    }
    NSUserDefaults *defaults=NSUserDefaults.standardUserDefaults;
    for(NSString *key in defaults.dictionaryRepresentation.allKeys) {
        if(![key hasPrefix:@"VIM"])continue;
        id value=[defaults objectForKey:key];id updated=RelocatedValue(value,moves);
        NSString *newKey=key;
        for(NSString *prefix in @[@"VIMLibraryKnown-",@"VIMLibraryOrder-",@"VIMLibraryArrange-"])
            if([key isEqual:[prefix stringByAppendingString:legacy.path]])newKey=[prefix stringByAppendingString:root.path];
        if([key hasPrefix:@"VIMGuestUsername-"])newKey=[@"VIMGuestUsername-" stringByAppendingString:RelocatedValue([key substringFromIndex:17],moves)];
        if([key isEqual:@"VIMCreationJob"]&&[updated isKindOfClass:NSDictionary.class]&&[updated[@"Library"] isEqual:legacy.path]){updated=[updated mutableCopy];updated[@"Library"]=root.path;}
        if(![key isEqual:newKey])[defaults removeObjectForKey:key];
        if(![key isEqual:newKey]||![value isEqual:updated])[defaults setObject:updated forKey:newKey];
    }
    if(!WriteLocationPlist(@{@"FormatVersion":@2},[root URLByAppendingPathComponent:LayoutMarker],error))return finish(NO);
    for(NSString *old in moves)if([old.pathExtension isEqual:@"vintelmac"]) {
        NSURL *url=[NSURL fileURLWithPath:moves[old]];
        NSString *identifier=[NSDictionary dictionaryWithContentsOfURL:[url URLByAppendingPathComponent:@"Machine.plist"]][@"Identifier"];
        if(identifier)dispatch_async(dispatch_get_main_queue(),^{[NSNotificationCenter.defaultCenter postNotificationName:@"VIMMachineRelocated" object:nil userInfo:@{@"OldURL":[NSURL fileURLWithPath:old],@"URL":url,@"Identifier":identifier}];});
    }
    // Keep the tiny migration journal as a recoverable path map.
    return finish(YES);
}

@implementation VIMLibraryLocation
+ (NSImage *)folderIcon {
    return [NSWorkspace.sharedWorkspace iconForFile:self.rootURL.path].copy;
}
+ (NSURL *)downloadsURL { return [self.rootURL URLByAppendingPathComponent:@"Downloads" isDirectory:YES]; }
+ (NSURL *)installersURL { return [self.rootURL URLByAppendingPathComponent:@"Installers" isDirectory:YES]; }
+ (NSURL *)rootURLForLibrary:(NSURL *)library {
    NSURL *url=library.URLByStandardizingPath;
    // Machines is a reserved child. Old preferences and resumable creation
    // jobs stored this child instead of the user-selected root. No marker is
    // needed to recognize it, including while a layout migration is deferred.
    return [url.lastPathComponent isEqual:@"Machines"]?url.URLByDeletingLastPathComponent:url;
}
+ (NSURL *)machinesURLForLibrary:(NSURL *)library {
    return [[self rootURLForLibrary:library] URLByAppendingPathComponent:@"Machines" isDirectory:YES];
}
+ (NSURL *)machinesURL { return [self machinesURLForLibrary:self.rootURL]; }
+ (NSURL *)downloadsURLForMachines:(NSURL *)machines {
    return [[self rootURLForLibrary:machines] URLByAppendingPathComponent:@"Downloads" isDirectory:YES];
}
+ (NSURL *)installersURLForMachines:(NSURL *)machines {
    return [[self rootURLForLibrary:machines] URLByAppendingPathComponent:@"Installers" isDirectory:YES];
}
+ (NSURL *)rootURL {
    NSString *configured=[NSUserDefaults.standardUserDefaults stringForKey:@"VIMLibraryPath"];
    NSURL *url=configured.length&&configured.isAbsolutePath?[NSURL fileURLWithPath:configured isDirectory:YES]:self.defaultURL;
    return [self rootURLForLibrary:url];
}
+ (NSURL *)currentURL { return self.rootURL; }
+ (NSURL *)defaultURL {
    NSURL *support=[NSFileManager.defaultManager URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask].firstObject;
    return [support URLByAppendingPathComponent:@"Virtual Intel Mac" isDirectory:YES];
}
+ (BOOL)prepareLayout:(NSError **)error {
    // Create the standard library even when the user currently uses an external
    // library. Never migrate that external library or change the selection here.
    NSFileManager *fm=NSFileManager.defaultManager;
    for(NSString *child in @[@"",@"Machines",@"Downloads",@"Installers"])
        if(![fm createDirectoryAtURL:[self.defaultURL URLByAppendingPathComponent:child] withIntermediateDirectories:YES attributes:nil error:error])return NO;
    NSString *iconVersion=@"VIMDefaultFolderIcon-v1";
    // Undo only the custom icon installed by the older app. New libraries use
    // their normal Finder icon, queried from AppKit rather than bundled art.
    if(NSThread.isMainThread&&[NSUserDefaults.standardUserDefaults boolForKey:iconVersion]) {
        if([NSWorkspace.sharedWorkspace setIcon:nil forFile:self.defaultURL.path options:0])
            [NSUserDefaults.standardUserDefaults removeObjectForKey:iconVersion];
    }
    NSString *configured=[NSUserDefaults.standardUserDefaults stringForKey:@"VIMLibraryPath"];
    NSURL *legacy=configured.length&&configured.isAbsolutePath?[NSURL fileURLWithPath:configured isDirectory:YES]:[self.defaultURL URLByAppendingPathComponent:@"Machines" isDirectory:YES];
    NSURL *root=self.rootURL;
    if(!PrepareRoot(root,legacy,error))return NO;
    NSUserDefaults *defaults=NSUserDefaults.standardUserDefaults;
    // Preserve the catalog and manual ordering from the Machines-keyed build.
    // Merge known rows: older root-keyed records can still describe renamed VMs.
    for(NSString *prefix in @[@"VIMLibraryKnown-",@"VIMLibraryOrder-",@"VIMLibraryArrange-"]) {
        NSString *old=[prefix stringByAppendingString:self.machinesURL.path],*key=[prefix stringByAppendingString:root.path];
        id previous=[defaults objectForKey:old];
        if(!previous)continue;
        if([prefix isEqual:@"VIMLibraryKnown-"]) {
            NSMutableDictionary *known=[[defaults dictionaryForKey:key] mutableCopy]?:[NSMutableDictionary new];
            if([previous isKindOfClass:NSDictionary.class])[known addEntriesFromDictionary:previous];
            [defaults setObject:known forKey:key];
        } else if(![defaults objectForKey:key])[defaults setObject:previous forKey:key];
        [defaults removeObjectForKey:old];
    }
    [defaults setObject:root.path forKey:@"VIMLibraryPath"];
    return YES;
}
+ (BOOL)setURL:(NSURL *)URL error:(NSError **)error {
    NSNumber *directory = nil;
    if (!URL.isFileURL || ![URL getResourceValue:&directory forKey:NSURLIsDirectoryKey error:error] || !directory.boolValue) {
        if (error && !*error) *error = [NSError errorWithDomain:@"VirtualIntelMac.Library" code:1
            userInfo:@{NSLocalizedDescriptionKey:@"Choose an existing folder for your Macs."}];
        return NO;
    }
    URL=[self rootURLForLibrary:URL];
    // A real exclusive probe catches read-only/offline volumes without relying
    // on an optimistic permissions check. Never touch existing user files.
    NSURL *probe = [URL URLByAppendingPathComponent:[@".vim-write-check-" stringByAppendingString:NSUUID.UUID.UUIDString]];
    if (![[NSData data] writeToURL:probe options:NSDataWritingWithoutOverwriting error:error]) return NO;
    if (![NSFileManager.defaultManager removeItemAtURL:probe error:error]) return NO;
    if(!PrepareRoot(URL,URL,error))return NO;
    [NSUserDefaults.standardUserDefaults setObject:URL.path forKey:@"VIMLibraryPath"];
    return [self prepareLayout:error];
}
+ (void)chooseFromWindow:(NSWindow *)window completion:(void (^)(BOOL))completion {
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.canChooseDirectories = YES; panel.canChooseFiles = NO; panel.canCreateDirectories = YES;
    panel.allowsMultipleSelection = NO; panel.directoryURL = self.rootURL;
    panel.prompt = @"Use This Folder";
    panel.message = @"This folder will contain Machines, Downloads and Installers. An external drive can save space on this Mac. Macs in other libraries stay where they are.";
    void (^selected)(NSModalResponse) = ^(NSModalResponse response) {
        if (response != NSModalResponseOK) { completion(NO); return; }
        NSError *error = nil;
        BOOL changed = [self setURL:panel.URL error:&error];
        if (!changed) VIMPresentError(error,window);
        completion(changed);
    };
    if (window) [panel beginSheetModalForWindow:window completionHandler:selected];
    else [panel beginWithCompletionHandler:selected];
}
@end
