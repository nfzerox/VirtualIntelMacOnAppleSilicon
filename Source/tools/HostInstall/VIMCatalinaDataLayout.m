#import "VIMCatalinaDataLayout.h"
#import "VIMAppleDouble.h"
#include <copyfile.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

static NSString *const DataTemplate=@"System/Library/Templates/Data";
static BOOL LayoutError(NSError **error,NSString *description) {
    if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.CatalinaDataLayout" code:1
        userInfo:@{NSLocalizedDescriptionKey:description}];
    return NO;
}
static BOOL RelativePath(NSString *path) {
    if(!path.length||path.isAbsolutePath)return NO;
    for(NSString *part in [path componentsSeparatedByString:@"/"])
        if(!part.length||[part isEqual:@"."]||[part isEqual:@".."])return NO;
    return YES;
}
static BOOL HasParentIn(NSString *path,NSSet<NSString *> *all) {
    for(NSString *parent=path.stringByDeletingLastPathComponent;parent.length;parent=parent.stringByDeletingLastPathComponent)
        if([all containsObject:parent])return YES;
    return NO;
}

static NSDictionary<NSString *,NSString *> *ParseFirmlinks(NSString *text,BOOL excludeInternal,NSError **error) {
    NSMutableDictionary<NSString *,NSString *> *links=[NSMutableDictionary dictionary];
    NSMutableSet<NSString *> *destinations=[NSMutableSet set];
    __block BOOL valid=YES;
    [text enumerateLinesUsingBlock:^(NSString *line,BOOL *stop){
        if(![line stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet].length)return;
        NSArray<NSString *> *fields=[line componentsSeparatedByString:@"\t"];
        if(fields.count!=2||![fields[0] hasPrefix:@"/"]){valid=NO;*stop=YES;return;}
        NSString *logical=[fields[0] substringFromIndex:1],*target=fields[1];
        // This internal entry does not exist on Apple's retail System volume.
        if(excludeInternal&&[logical isEqual:@"AppleInternal"]&&[target isEqual:@"AppleInternal"])return;
        if(!RelativePath(logical)||!RelativePath(target)||links[logical]||[destinations containsObject:target]) {
            valid=NO;*stop=YES;return;
        }
        links[logical]=target;[destinations addObject:target];
    }];
    NSSet *sources=[NSSet setWithArray:links.allKeys];
    if(valid)for(NSString *path in links)
        if(HasParentIn(path,sources)||HasParentIn(links[path],destinations)){valid=NO;break;}
    if(valid)for(NSString *path in @[@"Applications",@"Library",@"Users",@"private"])
        if(!links[path]){valid=NO;break;}
    if(valid)return links.copy;
    LayoutError(error,@"The original Catalina installer has an invalid System-to-Data manifest.");return nil;
}

NSDictionary<NSString *,NSString *> *VIMParseCatalinaFirmlinks(NSString *text,NSError **error) {
    return ParseFirmlinks(text,YES,error);
}
NSDictionary<NSString *,NSString *> *VIMParseSystemFirmlinks(NSString *text,NSError **error) {
    return ParseFirmlinks(text,NO,error);
}

NSString *VIMCatalinaDataPath(NSString *logical,NSDictionary<NSString *,NSString *> *links) {
    for(NSString *candidate=logical;candidate.length;candidate=candidate.stringByDeletingLastPathComponent) {
        NSString *target=links[candidate];
        if(target)return [target stringByAppendingString:[logical substringFromIndex:candidate.length]];
    }
    return nil;
}

static BOOL Directory(NSURL *root,NSString *path,NSMutableDictionary *metadata,NSError **error) {
    if(!path.length)return YES;
    NSURL *dir=[root URLByAppendingPathComponent:path];struct stat info;
    if(!lstat(dir.fileSystemRepresentation,&info))return S_ISDIR(info.st_mode)||
        LayoutError(error,@"A Catalina Data template directory has an unexpected file type.");
    if(errno!=ENOENT||!Directory(root,path.stringByDeletingLastPathComponent,metadata,error))return NO;
    if(![NSFileManager.defaultManager createDirectoryAtURL:dir withIntermediateDirectories:NO
        attributes:@{NSFilePosixPermissions:@0755} error:error])return NO;
    metadata[path]=@[@0,@0,@(S_IFDIR|0755)];return YES;
}

BOOL VIMMaterializeCatalinaDataTemplate(NSURL *root,NSDictionary<NSString *,NSString *> *links,
    NSMutableDictionary<NSString *,NSArray<NSNumber *> *> *metadata,
    BOOL (^copy)(NSURL *,NSURL *,NSError **),NSError **error) {
    // Catalina's original dslocal Default template is mode 0600. Open
    // only this private staging copy for traversal; retain the original
    // modes in the metadata to be written after volume projection.
    NSString *prefix=[DataTemplate stringByAppendingString:@"/"];
    NSArray<NSString *> *keys=[metadata.allKeys sortedArrayUsingSelector:@selector(compare:)];
    for(NSString *path in keys) {
        if(![path hasPrefix:prefix]&&![path isEqual:DataTemplate])continue;
        mode_t mode=metadata[path][2].unsignedShortValue;
        if(!S_ISDIR(mode)||(mode&0500)==0500)continue;
        NSURL *dir=[root URLByAppendingPathComponent:path];struct stat info;
        if(lstat(dir.fileSystemRepresentation,&info)||!S_ISDIR(info.st_mode)||
            chmod(dir.fileSystemRepresentation,(info.st_mode&07777)|0500))
            return LayoutError(error,@"Could not open a private Catalina template directory for staging.");
    }
    NSMutableDictionary<NSString *,NSString *> *materialized=[NSMutableDictionary dictionary];
    for(NSString *logical in [links.allKeys sortedArrayUsingSelector:@selector(compare:)]) {
        if(!Directory(root,logical,metadata,error))return NO;
        NSString *relative=[DataTemplate stringByAppendingPathComponent:links[logical]];
        NSURL *source=[root URLByAppendingPathComponent:relative];struct stat info;
        if(lstat(source.fileSystemRepresentation,&info)) {
            if(errno==ENOENT)continue;
            return LayoutError(error,@"Could not read an original Catalina Data template.");
        }
        if(!S_ISDIR(info.st_mode))continue;
        if(!copy(source,[root URLByAppendingPathComponent:logical],error))return NO;
        materialized[relative]=logical;
    }
    for(NSString *path in keys) {
        if(![path hasPrefix:prefix])continue;
        NSString *logical=VIMCatalinaDataPath(path,materialized);
        if(logical)metadata[logical]=metadata[path];
    }
    // Reproduce Core's createApplicationsSymlinks action.
    NSString *source=@"System/Applications/Utilities/System Information.app";
    NSString *logical=@"Applications/Utilities/System Information.app";
    if(!Directory(root,logical.stringByDeletingLastPathComponent,metadata,error))return NO;
    NSURL *alias=[root URLByAppendingPathComponent:logical];
    struct stat native,existing;
    if(!lstat([root URLByAppendingPathComponent:source].fileSystemRepresentation,&native)&&S_ISDIR(native.st_mode)&&
        lstat(alias.fileSystemRepresentation,&existing)&&errno==ENOENT) {
        if(symlink("../../System/Applications/Utilities/System Information.app",alias.fileSystemRepresentation))
            return LayoutError(error,@"Could not stage Catalina’s System Information alias.");
        metadata[logical]=@[@0,@0,@(S_IFLNK|0755)];
    }
    return YES;
}

typedef BOOL (^CopyTree)(NSURL *,NSURL *,NSError **);
static NSURL *At(NSURL *root,NSString *path) {return [path isEqual:@"."]?root:[root URLByAppendingPathComponent:path];}

// Each firmlink subtree was already materialized in the combined staging
// root before applying the account and drivers. Copy that version once.
// Only Data-only template files (e.g. home and mnt) need a separate copy.
static BOOL ProjectDataTemplate(NSURL *source,NSURL *data,NSString *path,NSSet *mapped,NSSet *parents,
                                CopyTree copy,NSError **error) {
    if([mapped containsObject:path])return YES;
    NSURL *original=At(source,path),*destination=At(data,path);
    if(![parents containsObject:path]) {
        struct stat info;
        if(lstat(original.fileSystemRepresentation,&info))return LayoutError(error,@"A Catalina Data template item disappeared.");
        if(S_ISLNK(info.st_mode))return !copyfile(original.fileSystemRepresentation,destination.fileSystemRepresentation,NULL,
            COPYFILE_ALL|COPYFILE_NOFOLLOW)||LayoutError(error,@"Could not copy a Catalina Data symlink.");
        return copy(original,destination,error);
    }
    NSArray<NSString *> *children=[NSFileManager.defaultManager contentsOfDirectoryAtPath:original.path error:error];
    if(!children)return NO;
    for(NSString *child in [children sortedArrayUsingSelector:@selector(compare:)]) {
        NSString *relative=[path isEqual:@"."]?child:[path stringByAppendingPathComponent:child];
        if(!ProjectDataTemplate(source,data,relative,mapped,parents,copy,error))return NO;
    }
    return !copyfile(original.fileSystemRepresentation,destination.fileSystemRepresentation,NULL,
        COPYFILE_METADATA|COPYFILE_NOFOLLOW)||LayoutError(error,@"Could not copy Catalina’s Data directory metadata.");
}

static BOOL ProjectSystem(NSURL *source,NSURL *system,NSString *path,NSDictionary *links,NSSet *ancestors,
                          NSDictionary *metadata,NSMutableDictionary *stubs,CopyTree copy,NSError **error) {
    NSURL *original=At(source,path),*destination=At(system,path);
    NSString *link=links[path];
    if(link||[ancestors containsObject:path]) {
        if(![path isEqual:@"."]) {
            if(![NSFileManager.defaultManager createDirectoryAtURL:destination withIntermediateDirectories:NO
                attributes:@{NSFilePosixPermissions:@0755} error:error])return NO;
            if(copyfile(original.fileSystemRepresentation,destination.fileSystemRepresentation,NULL,
                        COPYFILE_METADATA|COPYFILE_NOFOLLOW))
                return LayoutError(error,@"Could not copy Catalina’s System directory metadata.");
        }
        if(link) {
            const char *name=link.UTF8String;size_t length=strlen(name)+1;
            struct stat info;
            if(lstat(destination.fileSystemRepresentation,&info)||!S_ISDIR(info.st_mode)||
                setxattr(destination.fileSystemRepresentation,"com.apple.fs.firmlinj",name,length,0,XATTR_NOFOLLOW))
                return LayoutError(error,@"Could not reserve a Catalina System-to-Data link.");
            stubs[@(info.st_ino)]=[NSData dataWithBytes:name length:length];
            return YES;
        }
        NSArray *children=[NSFileManager.defaultManager contentsOfDirectoryAtPath:original.path error:error];
        if(!children)return NO;
        for(NSString *child in [children sortedArrayUsingSelector:@selector(compare:)]) {
            NSString *relative=[path isEqual:@"."]?child:[path stringByAppendingPathComponent:child];
            if(!ProjectSystem(source,system,relative,links,ancestors,metadata,stubs,copy,error))return NO;
        }
        return YES;
    }
    struct stat info;
    if(lstat(original.fileSystemRepresentation,&info))return LayoutError(error,@"A staged Catalina system item disappeared.");
    if([path isEqual:@".file"]&&S_ISREG(info.st_mode)&&info.st_size==0) {
        // The original root's mode-000 sentinel has no file data to read.
        // Copy it only if it belonged to the Apple payload, with its final
        // ownership and permissions applied by the detached finalizer.
        if(!metadata[path])return YES;
        int fd=open(destination.fileSystemRepresentation,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
        return (fd>=0&&!close(fd))||LayoutError(error,@"Could not create Catalina’s empty root sentinel.");
    }
    if(S_ISLNK(info.st_mode))return !copyfile(original.fileSystemRepresentation,destination.fileSystemRepresentation,NULL,
        COPYFILE_ALL|COPYFILE_NOFOLLOW)||LayoutError(error,@"Could not copy a Catalina system symlink.");
    return copy(original,destination,error);
}

static NSDictionary *PermissionsAt(NSURL *root,NSDictionary<NSString *,NSArray<NSNumber *> *> *paths,NSError **error) {
    NSMutableDictionary *permissions=[NSMutableDictionary dictionary];
    for(NSString *path in [paths.allKeys sortedArrayUsingSelector:@selector(compare:)]) @autoreleasepool {
        NSURL *file=At(root,path);struct stat info;
        if(lstat(file.fileSystemRepresentation,&info)) {
            NSString *leaf=path.lastPathComponent;
            if([leaf hasPrefix:@"._"]) {
                NSString *filePath=[path.stringByDeletingLastPathComponent stringByAppendingPathComponent:[leaf substringFromIndex:2]];
                if(paths[filePath]&&!lstat(At(root,filePath).fileSystemRepresentation,&info))continue;
            }
            LayoutError(error,[NSString stringWithFormat:@"Missing projected Catalina file %@.",path]);return nil;
        }
        NSArray<NSNumber *> *identity=paths[path];mode_t mode=identity[2].unsignedShortValue;
        NSNumber *inode=@(info.st_ino);NSArray *previous=permissions[inode];
        if((mode&S_IFMT)!=(info.st_mode&S_IFMT)||(previous&&![previous isEqual:identity])) {
            LayoutError(error,[NSString stringWithFormat:@"Unexpected projected Catalina file %@.",path]);return nil;
        }
        if(S_ISDIR(mode)&&!(info.st_mode&S_IXUSR)&&chmod(file.fileSystemRepresentation,(info.st_mode&07777)|S_IXUSR)) {
            LayoutError(error,@"Could not traverse a private Catalina scratch directory.");return nil;
        }
        permissions[inode]=identity;
    }
    return permissions;
}

static BOOL MakeStagedFilesReadable(NSURL *root,NSDictionary<NSString *,NSArray<NSNumber *> *> *metadata,NSError **error) {
    // Apple's catalog includes execute-only binaries and mode-000 files.
    // The private HFS staging volume is mounted without ownership and will
    // be discarded. Open only the owner's read/search bits here; the copied
    // APFS inodes get their original UID, GID and mode from metadata offline.
    for(NSString *path in [metadata.allKeys sortedArrayUsingSelector:@selector(compare:)]) @autoreleasepool {
        if(![path isEqual:@"."]&&!RelativePath(path))return LayoutError(error,@"A staged Catalina path is invalid.");
        mode_t original=metadata[path][2].unsignedShortValue;
        mode_t access=S_ISDIR(original)?S_IRUSR|S_IXUSR:S_ISREG(original)?S_IRUSR:0;
        if(!access||(original&access)==access)continue;
        NSURL *file=At(root,path);struct stat info;
        if(lstat(file.fileSystemRepresentation,&info)) {
            // Some package AppleDouble entries are merged by the extractor.
            // The projection's ordinary metadata pass checks missing files.
            if(errno==ENOENT)continue;
            return LayoutError(error,[NSString stringWithFormat:@"Could not read Catalina’s staged file %@.",path]);
        }
        if((info.st_mode&S_IFMT)!=(original&S_IFMT))continue;
        if((info.st_mode&access)!=access&&chmod(file.fileSystemRepresentation,(info.st_mode&07777)|access))
            return LayoutError(error,[NSString stringWithFormat:@"Could not make Catalina’s staged file %@ readable for copying.",path]);
    }
    return YES;
}

NSDictionary *VIMProjectCatalinaVolumes(NSURL *source,NSURL *system,NSURL *data,
    NSDictionary<NSString *,NSString *> *links,NSDictionary<NSString *,NSArray<NSNumber *> *> *metadata,
    CopyTree copy,NSError **error) {
    if(!MakeStagedFilesReadable(source,metadata,error))return nil;
    NSMutableDictionary *normalized=[metadata mutableCopy];
    VIMNormalizeAppleDoubleMetadata(source,normalized);metadata=normalized;
    NSFileManager *fm=NSFileManager.defaultManager;
    NSMutableSet *ancestors=[NSMutableSet setWithObject:@"."];
    NSMutableSet *dataParents=[NSMutableSet setWithObject:@"."];
    NSSet *mapped=[NSSet setWithArray:links.allValues];
    for(NSString *logical in [links.allKeys sortedArrayUsingSelector:@selector(compare:)]) {
        NSURL *destination=At(data,links[logical]);
        if(![fm createDirectoryAtURL:destination.URLByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:error]||
            !copy(At(source,logical),destination,error))return nil;
        for(NSString *parent=logical.stringByDeletingLastPathComponent;parent.length;parent=parent.stringByDeletingLastPathComponent)
            [ancestors addObject:parent];
        for(NSString *parent=[links[logical] stringByDeletingLastPathComponent];parent.length;parent=parent.stringByDeletingLastPathComponent)
            [dataParents addObject:parent];
    }
    if(!ProjectDataTemplate(At(source,DataTemplate),data,@".",mapped,dataParents,copy,error))return nil;
    NSMutableDictionary *stubs=[NSMutableDictionary dictionary];
    if(!ProjectSystem(source,system,@".",links,ancestors,metadata,stubs,copy,error))return nil;
    if(stubs.count!=links.count) {
        LayoutError(error,@"The new System volume is missing some of Catalina’s Data links.");return nil;
    }

    NSString *templatePrefix=[DataTemplate stringByAppendingString:@"/"];
    NSMutableDictionary *systemPaths=[NSMutableDictionary dictionary],*dataPaths=[NSMutableDictionary dictionary];
    if(metadata[DataTemplate])dataPaths[@"."]=metadata[DataTemplate];
    for(NSString *path in metadata)if([path hasPrefix:templatePrefix]) {
        NSString *relative=[path substringFromIndex:templatePrefix.length];
        if(![mapped containsObject:relative]&&!HasParentIn(relative,mapped))dataPaths[relative]=metadata[path];
    }
    for(NSString *path in metadata) {
        NSString *writable=VIMCatalinaDataPath(path,links);
        if(writable)dataPaths[writable]=metadata[path];
        if(!writable||links[path])systemPaths[path]=metadata[path];
    }
    NSDictionary *systemPermissions=PermissionsAt(system,systemPaths,error);
    if(!systemPermissions)return nil;
    NSDictionary *dataPermissions=PermissionsAt(data,dataPaths,error);
    return dataPermissions?@{@"SystemPermissions":systemPermissions,@"DataPermissions":dataPermissions,@"FirmlinkInodes":stubs}:nil;
}
