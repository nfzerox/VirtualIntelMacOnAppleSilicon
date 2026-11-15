#import "VIMLocalization.h"
#import "VIMDiskMaintenance.h"
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <copyfile.h>

static NSURL *activePackage;
static NSError *DiskError(NSString *message) {
    return [NSError errorWithDomain:@"VirtualIntelMac.DiskMaintenance" code:1 userInfo:@{NSLocalizedDescriptionKey:message}];
}
static BOOL Fail(NSError **error,NSString *message) {if(error)*error=DiskError(message);return NO;}
static BOOL SameFile(struct stat a,struct stat b) {
    return a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&a.st_size==b.st_size&&
        a.st_mtimespec.tv_sec==b.st_mtimespec.tv_sec&&a.st_mtimespec.tv_nsec==b.st_mtimespec.tv_nsec&&
        a.st_ctimespec.tv_sec==b.st_ctimespec.tv_sec&&a.st_ctimespec.tv_nsec==b.st_ctimespec.tv_nsec;
}
static uint64_t Allocated(struct stat info) {return (uint64_t)info.st_blocks*512;}

@interface VIMDiskMaintenance ()
@property NSURL *package;
@property NSString *identifier;
@property NSURL *qemuImage;
@property BOOL compressed;
@property int sessionLock;
@property(copy) void (^progress)(NSString *,double);
@end

@implementation VIMDiskMaintenance
+ (BOOL)hasActiveOperation {@synchronized(self){return activePackage!=nil;}}
+ (BOOL)isMaintainingPackage:(NSURL *)URL {
    @synchronized(self){return activePackage&&[activePackage isEqual:URL.URLByResolvingSymlinksInPath];}
}
+ (BOOL)startForPackage:(NSURL *)URL identifier:(NSString *)identifier qemuImage:(NSURL *)qemuImage
    compressed:(BOOL)compressed progress:(void (^)(NSString *,double))progress
    completion:(void (^)(NSDictionary *,NSError *))completion error:(NSError **)error {
    NSAssert(NSThread.isMainThread,@"Disk maintenance lifecycle is main-thread owned");
    if(!URL.isFileURL||!qemuImage.isFileURL||![[NSUUID alloc] initWithUUIDString:identifier])return Fail(error,VIMLocalized(@"diskmaintenance.this-mac-s-disk-maintenance-request-is-invalid", @"This Mac’s disk maintenance request is invalid."));
    @synchronized(self) {
        if(activePackage)return Fail(error,VIMLocalized(@"diskmaintenance.wait-for-the-current-disk-operation-to-finish-then-try-again", @"Wait for the current disk operation to finish, then try again."));
        activePackage=URL.URLByResolvingSymlinksInPath;
    }
    VIMDiskMaintenance *operation=[self new];operation.package=URL.URLByResolvingSymlinksInPath;
    operation.identifier=identifier;operation.qemuImage=qemuImage;operation.compressed=compressed;operation.progress=progress;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
        @autoreleasepool {
            NSError *failure=nil;NSDictionary *result=[operation perform:&failure];
            CFRunLoopPerformBlock(CFRunLoopGetMain(),kCFRunLoopCommonModes,^{
                @synchronized(self){activePackage=nil;}
                completion(result,failure);
            });
            CFRunLoopWakeUp(CFRunLoopGetMain());
        }
    });return YES;
}
- (void)report:(NSString *)phase fraction:(double)fraction {
    // A native modal loop can run inside an alert completion on the main queue.
    // Run-loop blocks continue delivering updates in that nested modal loop.
    CFRunLoopPerformBlock(CFRunLoopGetMain(),kCFRunLoopCommonModes,^{self.progress(phase,fraction);});
    CFRunLoopWakeUp(CFRunLoopGetMain());
}
// Drain the pipe while the child runs: waiting first deadlocks on a full pipe.
// Retain bounded output, since conversion reports progress using carriage returns.
- (NSData *)run:(NSArray *)arguments phase:(NSString *)phase error:(NSError **)error {
    NSTask *task=[NSTask new];task.executableURL=self.qemuImage;task.arguments=arguments;
    NSMutableDictionary *environment=[NSProcessInfo.processInfo.environment mutableCopy];environment[@"LC_ALL"]=@"C";task.environment=environment;
    NSPipe *pipe=[NSPipe pipe];task.standardOutput=pipe;task.standardError=pipe;
    // qemu-img does not read stdin. Inherit the session lock on fd 0 so an
    // orphaned conversion still excludes a new VM session if the app crashes.
    task.standardInput=[[NSFileHandle alloc] initWithFileDescriptor:self.sessionLock closeOnDealloc:NO];
    if(![task launchAndReturnError:error])return nil;
    NSMutableData *output=[NSMutableData data];double lastProgress=-1;
    NSRegularExpression *pattern=phase?[NSRegularExpression regularExpressionWithPattern:@"\\(([0-9]+(?:\\.[0-9]+)?)/100%\\)" options:0 error:nil]:nil;
    for(;;) {
        @autoreleasepool {
            NSData *chunk=pipe.fileHandleForReading.availableData;if(!chunk.length)break;
            [output appendData:chunk];
            if(pattern) {
                NSString *text=[[NSString alloc] initWithData:output encoding:NSUTF8StringEncoding];
                NSTextCheckingResult *match=[pattern matchesInString:text?:@"" options:0 range:NSMakeRange(0,text.length)].lastObject;
                if(match) {
                    double value=[[text substringWithRange:[match rangeAtIndex:1]] doubleValue]/100;
                    if(value>=lastProgress+.01&&value<=1){lastProgress=value;[self report:phase fraction:value];}
                }
            }
            if(output.length>65536)[output replaceBytesInRange:NSMakeRange(0,output.length-32768) withBytes:NULL length:0];
        }
    }
    [task waitUntilExit];[pipe.fileHandleForReading closeFile];
    if(task.terminationReason!=NSTaskTerminationReasonExit||task.terminationStatus!=0) {
        NSString *details=[[NSString alloc] initWithData:output encoding:NSUTF8StringEncoding]?:@"";
        if(details.length>2048)details=[details substringFromIndex:details.length-2048];
        Fail(error,[NSString stringWithFormat:VIMLocalized(@"diskmaintenance.the-disk-tool-could-not-finish", @"The disk tool could not finish %@. %@"),arguments.firstObject,details]);return nil;
    }return output;
}
- (NSDictionary *)JSON:(NSArray *)arguments error:(NSError **)error {
    NSData *output=[self run:arguments phase:nil error:error];if(!output)return nil;
    id value=[NSJSONSerialization JSONObjectWithData:output options:0 error:error];
    if(![value isKindOfClass:NSDictionary.class]){Fail(error,VIMLocalized(@"diskmaintenance.the-disk-tool-returned-an-invalid-result", @"The disk tool returned an invalid result."));return nil;}return value;
}
- (BOOL)plainImage:(NSDictionary *)info error:(NSError **)error {
    NSDictionary *specific=info[@"format-specific"],*data=[specific isKindOfClass:NSDictionary.class]?specific[@"data"]:nil;
    if(![info[@"format"] isEqual:@"qcow2"]||![info[@"virtual-size"] isKindOfClass:NSNumber.class]||
       [info[@"virtual-size"] longLongValue]<=0||![data isKindOfClass:NSDictionary.class])return Fail(error,VIMLocalized(@"diskmaintenance.choose-a-valid-qcow2-system-disk", @"Choose a valid QCOW2 system disk."));
    if([info[@"dirty-flag"] boolValue]||[data[@"corrupt"] boolValue])return Fail(error,VIMLocalized(@"diskmaintenance.this-disk-was-not-closed-cleanly-start-and-shut-down-the-virtual", @"This disk was not closed cleanly. Start and shut down the virtual Mac normally before trying again."));
    if(info[@"backing-filename"]||info[@"full-backing-filename"]||data[@"data-file"]||[info[@"encrypted"] boolValue]||data[@"encrypt"])
        return Fail(error,VIMLocalized(@"diskmaintenance.disk-maintenance-requires-an-unencrypted-independent-qcow2-disk-without-backing-or-external", @"Disk maintenance requires an unencrypted, independent QCOW2 disk without backing or external data files."));
    for(NSString *key in @[@"snapshots",@"bitmaps"]) {
        id value=[key isEqual:@"snapshots"]?info[key]:data[key];
        if(value&&(![value isKindOfClass:NSArray.class]||[value count]))return Fail(error,VIMLocalized(@"diskmaintenance.this-disk-contains-snapshots-or-persistent-bitmaps-disk-maintenance-would-discard-them", @"This disk contains snapshots or persistent bitmaps. Disk maintenance would discard them."));
    }return YES;
}
- (BOOL)removeInterruptedStages:(NSURL *)directory disk:(NSString *)relative error:(NSError **)error {
    NSFileManager *fm=NSFileManager.defaultManager;
    NSArray *items=[fm contentsOfDirectoryAtURL:directory includingPropertiesForKeys:nil options:0 error:error];if(!items)return NO;
    for(NSURL *item in items) {
        NSString *name=item.lastPathComponent,*prefix=@".DiskMaintenance-";
        if(![name hasPrefix:prefix]||![[NSUUID alloc] initWithUUIDString:[name substringFromIndex:prefix.length]])continue;
        struct stat info;
        if(lstat(item.fileSystemRepresentation,&info)||!S_ISDIR(info.st_mode)||info.st_uid!=geteuid()||(info.st_mode&0777)!=0700)continue;
        NSURL *record=[item URLByAppendingPathComponent:@"Operation.plist"];
        if(lstat(record.fileSystemRepresentation,&info)||!S_ISREG(info.st_mode)||info.st_nlink!=1||info.st_uid!=geteuid())continue;
        NSDictionary *receipt=[NSDictionary dictionaryWithContentsOfURL:record];
        if(![receipt[@"FormatVersion"] isEqual:@1]||![receipt[@"Identifier"] isEqual:self.identifier]||![receipt[@"Disk"] isEqual:relative])continue;
        NSArray *children=[fm contentsOfDirectoryAtURL:item includingPropertiesForKeys:nil options:0 error:nil];
        BOOL owned=children!=nil;
        for(NSURL *child in children) {
            if(![@[@"Image.qcow2",@"Operation.plist"] containsObject:child.lastPathComponent]||lstat(child.fileSystemRepresentation,&info)||
               !S_ISREG(info.st_mode)||info.st_nlink!=1||info.st_uid!=geteuid()){owned=NO;break;}
        }
        // The session lock is also inherited by qemu-img. Acquiring it proves
        // the interrupted app's child has finished using its private staging.
        if(owned&&![fm removeItemAtURL:item error:error])return NO;
    }return YES;
}
- (NSDictionary *)perform:(NSError **)error {
    NSFileManager *fm=NSFileManager.defaultManager;
    NSURL *state=[self.package URLByAppendingPathComponent:@"State"],*configuration=[self.package URLByAppendingPathComponent:@"Machine.plist"];
    struct stat info;
    if(lstat(state.fileSystemRepresentation,&info)||!S_ISDIR(info.st_mode)){Fail(error,VIMLocalized(@"diskmaintenance.this-mac-s-state-folder-is-unavailable", @"This Mac’s State folder is unavailable."));return nil;}
    NSURL *lockURL=[state URLByAppendingPathComponent:@"Session.lock"];
    int lock=open(lockURL.fileSystemRepresentation,O_CREAT|O_RDWR|O_NOFOLLOW|O_CLOEXEC,0600);
    if(lock<0||fstat(lock,&info)||!S_ISREG(info.st_mode)||info.st_nlink!=1||flock(lock,LOCK_EX|LOCK_NB)) {
        if(lock>=0)close(lock);Fail(error,VIMLocalized(@"diskmaintenance.shut-down-this-mac-and-wait-for-its-other-disk-or-session", @"Shut down this Mac and wait for its other disk or session operations to finish, then try again."));return nil;
    }
    self.sessionLock=lock;
    int source=-1,parent=-1;NSURL *stage=nil;
    @try {
        [self report:VIMLocalized(@"diskmaintenance.checking-disk", @"Checking disk…") fraction:-1];
        NSDictionary *config=[NSDictionary dictionaryWithContentsOfURL:configuration];
        NSString *relative=config[@"Disk"];
        if(![config[@"Identifier"] isEqual:self.identifier]||![relative isKindOfClass:NSString.class]||!relative.length||relative.isAbsolutePath||
            [relative.pathComponents containsObject:@".."]||![relative isEqual:relative.stringByStandardizingPath]) {
            Fail(error,VIMLocalized(@"diskmaintenance.this-mac-s-disk-configuration-changed-close-its-settings-and-try-again", @"This Mac’s disk configuration changed. Close its settings and try again."));return nil;
        }
        NSURL *disk=[self.package URLByAppendingPathComponent:relative],*directory=disk.URLByDeletingLastPathComponent;
        if(![disk.URLByResolvingSymlinksInPath.path isEqual:disk.path]||![disk.path hasPrefix:[self.package.path stringByAppendingString:@"/"]]) {
            Fail(error,VIMLocalized(@"diskmaintenance.disk-maintenance-cannot-follow-links-or-operate-outside-this-mac-s-package", @"Disk maintenance cannot follow links or operate outside this Mac’s package."));return nil;
        }
        source=open(disk.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
        struct stat original;
        if(source<0||fstat(source,&original)||!S_ISREG(original.st_mode)||original.st_nlink!=1||original.st_uid!=geteuid()) {
            Fail(error,VIMLocalized(@"diskmaintenance.the-system-disk-is-unavailable-shared-through-a-hard-link-or-owned", @"The system disk is unavailable, shared through a hard link, or owned by another user."));return nil;
        }
        parent=open(directory.fileSystemRepresentation,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        if(parent<0||fsync(parent)){Fail(error,VIMLocalized(@"diskmaintenance.this-drive-cannot-safely-publish-a-replacement-disk-image", @"This drive cannot safely publish a replacement disk image."));return nil;}
        if(![self removeInterruptedStages:directory disk:relative error:error])return nil;
        NSDictionary *metadata=[self JSON:@[@"info",@"--output=json",@"-f",@"qcow2",disk.path] error:error];
        if(!metadata||![self plainImage:metadata error:error])return nil;
        if(![self run:@[@"check",@"-f",@"qcow2",disk.path] phase:nil error:error])return nil;
        NSDictionary *measure=[self JSON:@[@"measure",@"--output=json",@"-f",@"qcow2",@"-O",@"qcow2",disk.path] error:error];
        if(!measure)return nil;
        NSNumber *required=measure[@"required"];
        NSDictionary *space=[fm attributesOfFileSystemForPath:directory.path error:error];
        uint64_t available=[space[NSFileSystemFreeSize] unsignedLongLongValue],needed=required.unsignedLongLongValue;
        if(![required isKindOfClass:NSNumber.class]||required.longLongValue<=0||!space||available<needed||available-needed<(1ULL<<30)) {
            Fail(error,VIMLocalized(@"diskmaintenance.this-drive-needs-more-free-space-for-a-temporary-disk-image-and", @"This drive needs more free space for a temporary disk image and 1 GB of working space. The original disk has not been replaced."));return nil;
        }
        stage=[directory URLByAppendingPathComponent:[@".DiskMaintenance-" stringByAppendingString:NSUUID.UUID.UUIDString]];
        if(![fm createDirectoryAtURL:stage withIntermediateDirectories:NO attributes:@{NSFilePosixPermissions:@0700} error:error]){stage=nil;return nil;}
        NSURL *record=[stage URLByAppendingPathComponent:@"Operation.plist"];
        if(![@{@"FormatVersion":@1,@"Identifier":self.identifier,@"Disk":relative} writeToURL:record error:error])return nil;
        NSURL *target=[stage URLByAppendingPathComponent:@"Image.qcow2"];
        NSString *phase=self.compressed?VIMLocalized(@"diskmaintenance.compressing-disk", @"Compressing disk…"):VIMLocalized(@"diskmaintenance.reclaiming-disk-space", @"Reclaiming disk space…");[self report:phase fraction:0];
        NSMutableArray *arguments=[@[@"convert",@"-p",@"-f",@"qcow2",@"-O",@"qcow2",@"-S",@"4k"] mutableCopy];
        if(self.compressed)[arguments addObjectsFromArray:@[@"-c",@"-o",@"compression_type=zlib"]];
        [arguments addObjectsFromArray:@[disk.path,target.path]];
        if(![self run:arguments phase:phase error:error])return nil;
        [self report:VIMLocalized(@"diskmaintenance.verifying-disk", @"Verifying disk…") fraction:-1];
        NSDictionary *converted=[self JSON:@[@"info",@"--output=json",@"-f",@"qcow2",target.path] error:error];
        if(!converted||![self plainImage:converted error:error])return nil;
        if(![converted[@"virtual-size"] isEqual:metadata[@"virtual-size"]]){Fail(error,VIMLocalized(@"diskmaintenance.the-converted-disk-has-a-different-capacity-the-original-disk-has-been", @"The converted disk has a different capacity. The original disk has been kept."));return nil;}
        if(![self run:@[@"check",@"-f",@"qcow2",target.path] phase:nil error:error]||
           ![self run:@[@"compare",@"-p",@"-f",@"qcow2",@"-F",@"qcow2",disk.path,target.path] phase:VIMLocalized(@"diskmaintenance.verifying-disk", @"Verifying disk…") error:error])return nil;
        NSDictionary *latest=[NSDictionary dictionaryWithContentsOfURL:configuration];
        if(![latest[@"Identifier"] isEqual:self.identifier]||![latest[@"Disk"] isEqual:relative]) {
            Fail(error,VIMLocalized(@"diskmaintenance.this-mac-s-disk-configuration-changed-during-maintenance-the-original-disk-has", @"This Mac’s disk configuration changed during maintenance. The original disk has been kept."));return nil;
        }
        struct stat current,replacement;
        if(lstat(disk.fileSystemRepresentation,&current)||!SameFile(original,current)||lstat(target.fileSystemRepresentation,&replacement)||!S_ISREG(replacement.st_mode)||replacement.st_nlink!=1) {
            Fail(error,VIMLocalized(@"diskmaintenance.the-disk-changed-during-maintenance-the-original-disk-has-not-been-replaced", @"The disk changed during maintenance. The original disk has not been replaced."));return nil;
        }
        uint64_t before=Allocated(original),after=Allocated(replacement);
        if(after>=before)return @{@"BeforeBytes":@(before),@"AfterBytes":@(before),@"Changed":@NO};
        int output=open(target.fileSystemRepresentation,O_RDWR|O_NOFOLLOW|O_CLOEXEC);
        // Keep ACLs, extended attributes, ownership and mode, not only bytes.
        BOOL flushed=output>=0&&fcopyfile(source,output,NULL,COPYFILE_METADATA)==0&&fstat(output,&replacement)==0&&fsync(output)==0&&fcntl(output,F_FULLFSYNC)==0;
        if(output>=0)close(output);
        if(!flushed){Fail(error,VIMLocalized(@"diskmaintenance.the-new-disk-could-not-be-saved-safely-the-original-disk-has", @"The new disk could not be saved safely. The original disk has been kept."));return nil;}
        after=Allocated(replacement);
        if(after>=before)return @{@"BeforeBytes":@(before),@"AfterBytes":@(before),@"Changed":@NO};
        if(lstat(disk.fileSystemRepresentation,&current)||!SameFile(original,current)) {
            Fail(error,VIMLocalized(@"diskmaintenance.the-disk-changed-before-publication-the-original-disk-has-not-been-replaced", @"The disk changed before publication. The original disk has not been replaced."));return nil;
        }
        // One same-volume rename is the only publication step. After an
        // interruption, the configured path still names a complete disk.
        [self report:VIMLocalized(@"diskmaintenance.finishing", @"Finishing…") fraction:-1];
        if(rename(target.fileSystemRepresentation,disk.fileSystemRepresentation)) {
            Fail(error,VIMLocalized(@"diskmaintenance.the-new-disk-could-not-replace-the-original-the-original-disk-has", @"The new disk could not replace the original. The original disk has been kept."));return nil;
        }
        if(fsync(parent)){Fail(error,VIMLocalized(@"diskmaintenance.the-verified-disk-was-replaced-but-this-drive-could-not-confirm-that", @"The verified disk was replaced, but this drive could not confirm that the replacement was saved. Keep the drive connected."));return nil;}
        return @{@"BeforeBytes":@(before),@"AfterBytes":@(after),@"Changed":@YES};
    } @finally {
        if(stage)[fm removeItemAtURL:stage error:nil];
        if(parent>=0)close(parent);if(source>=0)close(source);close(lock);
    }
}
@end
