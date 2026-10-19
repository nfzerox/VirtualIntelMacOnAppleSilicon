// Boot-cache snapshots are separate from driver publication: kextcache writes
// these files itself, so their replacement hashes cannot be fixed in advance.
#include <CommonCrypto/CommonDigest.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/time.h>
#include <dlfcn.h>
static NSArray<NSString *> *BootCacheArguments(NSString *root,BOOL mojave,BOOL untrustedKexts,BOOL unrestrictedFS) {
    // Mojave kext_tools-528.250.3: -i forks a normal authenticated prelinker
    // (bootcaches.c:rebuild_kext_boot_cache_file). Only its parent then refreshes
    // driver-information plists with the selected authentication options.
    // Avoid repeating expensive certificate/XPC checks for that metadata in
    // our explicitly SIP-disabled guest; never apply this to host/offline roots
    // or assume Catalina's cache-builder implementation has the same behavior.
    BOOL fast=mojave&&[root isEqual:@"/"]&&untrustedKexts&&unrestrictedFS;
    return fast?@[@"-z",@"-i",root]:@[@"-i",root];
}
static NSString *CacheHash(NSString *path) {
    int fd=open(path.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    Check(fd>=0,@"Cannot open boot cache");struct stat st;
    if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<=0){close(fd);Check(NO,@"Boot cache is not a nonempty regular file");}
    CC_SHA256_CTX hash;CC_SHA256_Init(&hash);unsigned char buffer[65536],digest[32];
    for(;;){ssize_t size=read(fd,buffer,sizeof(buffer));if(size<0&&errno==EINTR)continue;
        if(size<=0){close(fd);Check(size==0,@"Boot cache read failed");break;}CC_SHA256_Update(&hash,buffer,(CC_LONG)size);}
    CC_SHA256_Final(digest,&hash);NSMutableString *result=[NSMutableString string];
    for(unsigned i=0;i<32;i++)[result appendFormat:@"%02x",digest[i]];return result;
}
static NSArray<NSString *> *CachePaths(BOOL mojave) {
    NSString *system=@"System/Library/PrelinkedKernels/prelinkedkernel";
    return mojave?@[system]:@[system,@"Library/Apple/System/Library/PrelinkedKernels/prelinkedkernel"];
}
static NSDictionary *SnapshotBootCaches(NSURL *root,NSURL *directory,BOOL mojave,NSString *build) {
    NSFileManager *fm=NSFileManager.defaultManager;NSError *error=nil;
    Check(![fm fileExistsAtPath:directory.path],@"Boot-cache snapshot already exists");
    Check([fm createDirectoryAtURL:directory withIntermediateDirectories:NO attributes:@{NSFilePosixPermissions:@0700} error:&error],error.localizedDescription);
    NSMutableArray *records=[NSMutableArray array];
    for(NSString *relative in CachePaths(mojave)) {
        NSURL *source=[root URLByAppendingPathComponent:relative];struct stat st;
        BOOL present=lstat(source.fileSystemRepresentation,&st)==0;
        Check(present||errno==ENOENT,@"Cannot inspect boot cache");
        Check(present||records.count>0,@"No System-volume prelinked kernel; unsupported boot layout");
        NSMutableDictionary *record=[@{@"RelativePath":relative,@"Present":@(present)} mutableCopy];
        if(present) {
            NSString *hash=CacheHash(source.path);
            NSString *name=[NSString stringWithFormat:@"cache-%lu",(unsigned long)records.count];
            NSURL *saved=[directory URLByAppendingPathComponent:name];
            Check([fm copyItemAtURL:source toURL:saved error:&error],error.localizedDescription);
            Check([CacheHash(saved.path) isEqual:hash]&&[CacheHash(source.path) isEqual:hash],@"Boot cache changed while snapshotting");
            int fd=open(saved.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW);Check(fd>=0,@"Cannot flush saved boot cache");
            int result=fsync(fd);if(!result)result=fcntl(fd,F_FULLFSYNC);close(fd);Check(result==0,@"Cannot durably save boot cache");
            record[@"File"]=name;record[@"SHA256"]=hash;record[@"Mode"]=@(st.st_mode&07777);
            record[@"UID"]=@(st.st_uid);record[@"GID"]=@(st.st_gid);
        }
        [records addObject:record];
    }
    NSDictionary *snapshot=@{@"FormatVersion":@1,@"ProductBuildVersion":build,@"Root":root.path,@"Caches":records};
    NSData *data=[NSPropertyListSerialization dataWithPropertyList:snapshot format:NSPropertyListBinaryFormat_v1_0 options:0 error:&error];
    NSURL *receipt=[directory URLByAppendingPathComponent:@"Snapshot.plist"];
    Check(data&&[data writeToURL:receipt options:NSDataWritingAtomic error:&error],error.localizedDescription);
    int fd=open(receipt.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW);Check(fd>=0,@"Cannot flush cache snapshot receipt");
    int result=fsync(fd);if(!result)result=fcntl(fd,F_FULLFSYNC);close(fd);Check(result==0,@"Cannot durably save cache snapshot receipt");
    return snapshot;
}
static NSDictionary *VerifyBootCacheSnapshot(NSURL *root,NSURL *directory,BOOL mojave,NSString *build) {
    NSDictionary *snapshot=[NSDictionary dictionaryWithContentsOfURL:[directory URLByAppendingPathComponent:@"Snapshot.plist"]];
    NSDictionary *version=[NSDictionary dictionaryWithContentsOfURL:[root URLByAppendingPathComponent:@"System/Library/CoreServices/SystemVersion.plist"]];
    Check([snapshot[@"FormatVersion"] isEqual:@1]&&[snapshot[@"ProductBuildVersion"] isEqual:build]&&
          [version[@"ProductBuildVersion"] isEqual:build]&&[snapshot[@"Root"] isEqual:root.path],@"Boot-cache backup belongs to a different system/build");
    NSArray *paths=CachePaths(mojave),*records=snapshot[@"Caches"];
    Check([records isKindOfClass:NSArray.class]&&records.count==paths.count,@"Invalid boot-cache backup layout");
    // Validate every retained cache before touching any current cache.
    for(NSUInteger i=0;i<paths.count;i++) {
        NSDictionary *record=records[i];Check([record isKindOfClass:NSDictionary.class]&&[record[@"RelativePath"] isEqual:paths[i]],@"Unexpected boot-cache target");
        Check([record[@"Present"] isKindOfClass:NSNumber.class],@"Invalid cache presence record");
        if([record[@"Present"] boolValue]) {
            NSString *expected=[NSString stringWithFormat:@"cache-%lu",(unsigned long)i];
            Check([record[@"File"] isEqual:expected]&&[CacheHash([directory URLByAppendingPathComponent:expected].path) isEqual:record[@"SHA256"]],@"Saved boot cache failed integrity verification");
            for(NSString *key in @[@"Mode",@"UID",@"GID"])Check([record[key] isKindOfClass:NSNumber.class],@"Invalid saved cache attributes");
        }
    }
    Check([records[0][@"Present"] isEqual:@YES],@"System boot-cache backup is missing");
    return snapshot;
}
static void RestoreBootCaches(NSURL *root,NSURL *directory,BOOL mojave,NSString *build) {
    NSFileManager *fm=NSFileManager.defaultManager;NSError *error=nil;
    NSDictionary *snapshot=VerifyBootCacheSnapshot(root,directory,mojave,build);
    NSArray *paths=CachePaths(mojave),*records=snapshot[@"Caches"];
    for(NSUInteger i=0;i<paths.count;i++) {
        NSDictionary *record=records[i];NSURL *target=[root URLByAppendingPathComponent:paths[i]];
        NSString *parent=target.path.stringByDeletingLastPathComponent;
        Check([root.path isEqual:@"/"]||[[parent stringByResolvingSymlinksInPath] hasPrefix:[root.path stringByAppendingString:@"/"]],@"Boot-cache parent escaped guest root");
        struct stat st;BOOL present=lstat(target.fileSystemRepresentation,&st)==0;
        Check(present||errno==ENOENT,@"Cannot inspect current boot cache");
        if(present)Check(S_ISREG(st.st_mode),@"Refusing to replace a nonregular boot cache");
        if([record[@"Present"] boolValue]) {
            if(present&&st.st_size>0&&[CacheHash(target.path) isEqual:record[@"SHA256"]])continue;
            NSURL *candidate=[NSURL fileURLWithPath:[parent stringByAppendingPathComponent:[@".vim-cache-restore-" stringByAppendingString:NSUUID.UUID.UUIDString]]];
            Check([fm copyItemAtURL:[directory URLByAppendingPathComponent:record[@"File"]] toURL:candidate error:&error],error.localizedDescription);
            Check([CacheHash(candidate.path) isEqual:record[@"SHA256"]],@"Restored cache copy failed verification");
            int fd=open(candidate.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW);Check(fd>=0,@"Cannot open restored cache");
            BOOL attributes=fchown(fd,[record[@"UID"] unsignedIntValue],[record[@"GID"] unsignedIntValue])==0&&fchmod(fd,[record[@"Mode"] unsignedIntValue])==0;
            int flushed=attributes?fsync(fd):-1;if(!flushed)flushed=fcntl(fd,F_FULLFSYNC);close(fd);
            Check(attributes&&flushed==0,@"Cannot restore boot-cache attributes or flush data");
            // Preserve the cache being withdrawn, including a builder's partial
            // output. Renames stay beside the target on its own APFS volume.
            if(present) {
                NSString *retained=[parent stringByAppendingPathComponent:[@".vim-cache-withdrawn-" stringByAppendingString:NSUUID.UUID.UUIDString]];
                Check(rename(target.fileSystemRepresentation,retained.fileSystemRepresentation)==0,@"Cannot retain current boot cache");
            }
            Check(rename(candidate.fileSystemRepresentation,target.fileSystemRepresentation)==0,@"Cannot publish restored boot cache");
        } else if(present) {
            NSString *retained=[parent stringByAppendingPathComponent:[@".vim-cache-withdrawn-" stringByAppendingString:NSUUID.UUID.UUIDString]];
            Check(rename(target.fileSystemRepresentation,retained.fileSystemRepresentation)==0,@"Cannot withdraw newly generated boot cache");
        }
        if([fm fileExistsAtPath:parent]) {
            int fd=open(parent.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW);Check(fd>=0,@"Cannot flush boot-cache parent");
            int result=fsync(fd);close(fd);Check(result==0,@"Cannot flush restored boot-cache directory");
        }
    }
}
static NSDictionary *RebuildBootCaches(NSURL *root,BOOL mojave,NSURL *log) {
    NSString *executable=@"/usr/sbin/kextcache";
    BOOL untrustedKexts=NO,unrestrictedFS=NO;
#ifdef VIM_INSTALL_PLAN_TESTING
    Check(![root.path isEqual:@"/"],@"Fixture cache builder cannot target live root");
    const char *fixture=getenv("VIM_TEST_CACHE_BUILDER");Check(fixture!=NULL,@"Missing fixture cache builder");executable=@(fixture);
#else
    Check([root.path isEqual:@"/"]&&geteuid()==0,@"Boot cache rebuilding requires the running guest and root privileges");
    int (*csrCheck)(uint32_t)=dlsym(RTLD_DEFAULT,"csr_check");
    if(csrCheck){untrustedKexts=csrCheck(1U<<0)==0;unrestrictedFS=csrCheck(1U<<1)==0;}
#endif
    int logFD=open(log.fileSystemRepresentation,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
    Check(logFD>=0,@"Cache build log already exists or cannot be created");
    NSFileHandle *output=[[NSFileHandle alloc] initWithFileDescriptor:logFD closeOnDealloc:YES];
    for(NSString *relative in @[@"System/Library/Extensions",@"Library/Extensions"])
        Check(utimes([root URLByAppendingPathComponent:relative].fileSystemRepresentation,NULL)==0,@"Cannot update extensions timestamp");
    struct timespec started;clock_gettime(CLOCK_REALTIME,&started);
    NSArray *arguments=BootCacheArguments(root.path,mojave,untrustedKexts,unrestrictedFS);
    double start=NSProcessInfo.processInfo.systemUptime;
    NSTask *task=[NSTask new];task.executableURL=[NSURL fileURLWithPath:executable];task.arguments=arguments;
    task.standardOutput=output;task.standardError=output;NSError *error=nil;
    Check([task launchAndReturnError:&error],error.localizedDescription);
    // Under TCG the driver-information caches refreshed after prelinking can
    // take several more minutes. Keep the full -i operation, bounded, rather
    // than accepting a cache file while its builder is still running.
    double deadline=NSProcessInfo.processInfo.systemUptime+600;
    while(task.running&&NSProcessInfo.processInfo.systemUptime<deadline)usleep(10000);
    if(task.running){kill(task.processIdentifier,SIGKILL);[task waitUntilExit];Check(NO,@"Boot-cache rebuild exceeded ten minutes; rollback is required");}
    [task waitUntilExit];[output synchronizeFile];[output closeFile];
    Check(task.terminationReason==NSTaskTerminationReasonExit&&task.terminationStatus==0,@"Boot-cache builder failed; rollback is required");
    NSArray *paths=CachePaths(mojave);NSURL *generated=[root URLByAppendingPathComponent:paths.lastObject];
    struct stat st;Check(lstat(generated.fileSystemRepresentation,&st)==0&&S_ISREG(st.st_mode)&&st.st_size>0,@"Builder produced no nonempty boot cache");
    Check(st.st_mtimespec.tv_sec>started.tv_sec||(st.st_mtimespec.tv_sec==started.tv_sec&&st.st_mtimespec.tv_nsec>=started.tv_nsec),@"Builder did not produce a fresh boot cache");
    NSString *hash=CacheHash(generated.path);
    int generatedFD=open(generated.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW);Check(generatedFD>=0,@"Cannot flush generated cache");
    int generatedFlush=fsync(generatedFD);if(!generatedFlush)generatedFlush=fcntl(generatedFD,F_FULLFSYNC);close(generatedFD);
    Check(generatedFlush==0,@"Cannot persist generated boot cache");
    if(!mojave) {
        NSURL *system=[root URLByAppendingPathComponent:paths[0]];
        NSURL *candidate=[system.URLByDeletingLastPathComponent URLByAppendingPathComponent:[@".vim-cache-publish-" stringByAppendingString:NSUUID.UUID.UUIDString]];
        Check([NSFileManager.defaultManager copyItemAtURL:generated toURL:candidate error:&error],error.localizedDescription);
        Check([CacheHash(candidate.path) isEqual:hash],@"System cache copy failed integrity check");
        int fd=open(candidate.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW);Check(fd>=0,@"Cannot flush rebuilt cache");
        int result=fsync(fd);if(!result)result=fcntl(fd,F_FULLFSYNC);close(fd);Check(result==0,@"Cannot flush rebuilt cache");
        Check(rename(candidate.fileSystemRepresentation,system.fileSystemRepresentation)==0,@"Cannot publish System boot cache");
        int parent=open(system.URLByDeletingLastPathComponent.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW);Check(parent>=0,@"Cannot flush boot-cache directory");
        result=fsync(parent);close(parent);Check(result==0,@"Cannot persist System boot-cache rename");
    }
    return @{@"CacheRebuilt":@YES,@"SHA256":hash,@"Size":@(st.st_size),@"Ready":@NO,
        @"BuilderArguments":arguments,@"BuildSeconds":@(NSProcessInfo.processInfo.systemUptime-start)};
}
