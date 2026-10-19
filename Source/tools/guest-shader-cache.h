// Compiled shader caches do not identify our compiler/ABI bridge revision.
// Preserve them under a new name when publishing a different graphics stack.
// This does not remove documents, application preferences, or dyld caches.
#import <Foundation/Foundation.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <limits.h>

static NSDictionary *QuarantineGuestShaderCaches(NSURL *root) {
    NSURL *folders=[root.URLByResolvingSymlinksInPath URLByAppendingPathComponent:@"private/var/folders" isDirectory:YES];
    NSMutableArray *moved=[NSMutableArray array],*failures=[NSMutableArray array];
    int base=open(folders.fileSystemRepresentation,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(base<0) {
        if(errno!=ENOENT)[failures addObject:@"Cannot open guest shader cache directory"];
        return @{@"Quarantined":moved,@"Errors":failures};
    }
    char canonical[PATH_MAX];
    if(!realpath(folders.fileSystemRepresentation,canonical)) {
        close(base);return @{@"Quarantined":moved,@"Errors":@[@"Cannot resolve guest cache directory"]};
    }
    folders=[NSURL fileURLWithFileSystemRepresentation:canonical isDirectory:YES relativeToURL:nil];
    NSDirectoryEnumerator *entries=[NSFileManager.defaultManager enumeratorAtURL:folders
        includingPropertiesForKeys:@[NSURLIsSymbolicLinkKey,NSURLIsDirectoryKey]
        options:NSDirectoryEnumerationSkipsHiddenFiles errorHandler:^BOOL(NSURL *url,NSError *error){
            (void)url;
            [failures addObject:error.localizedDescription];return YES;
        }];
    for(NSURL *entry in entries) {
        NSArray *parts=[[entry.path substringFromIndex:folders.path.length+1] pathComponents];
        NSNumber *symlink=nil,*directory=nil;
        [entry getResourceValue:&symlink forKey:NSURLIsSymbolicLinkKey error:NULL];
        [entry getResourceValue:&directory forKey:NSURLIsDirectoryKey error:NULL];
        if(symlink.boolValue||parts.count>=5)[entries skipDescendants];
        if(!directory.boolValue||symlink.boolValue||![entry.lastPathComponent isEqual:@"com.apple.metal"]||
           (parts.count!=4&&parts.count!=5)||![parts[2] isEqual:@"C"])continue;
        [entries skipDescendants];
        // Walk by directory descriptors: never follow a user's cache symlink
        // or let a concurrent path replacement redirect privileged renames.
        int parent=dup(base);
        for(NSUInteger i=0;parent>=0&&i+1<parts.count;i++) {
            int next=openat(parent,[parts[i] fileSystemRepresentation],O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
            close(parent);parent=next;
        }
        if(parent<0){[failures addObject:@"Shader cache parent changed during inspection"];continue;}
        struct stat st;
        if(fstatat(parent,"com.apple.metal",&st,AT_SYMLINK_NOFOLLOW)||!S_ISDIR(st.st_mode)) {
            close(parent);[failures addObject:@"Shader cache changed during inspection"];continue;
        }
        NSString *backup=[@".com.apple.metal.virtualintelmac-" stringByAppendingString:NSUUID.UUID.UUIDString];
        // An unpredictable, non-existing sibling keeps the move atomic on
        // the original filesystem; no cross-volume copy or recursive delete.
        struct stat existing;
        if(!fstatat(parent,backup.fileSystemRepresentation,&existing,AT_SYMLINK_NOFOLLOW)||errno!=ENOENT||
           renameat(parent,"com.apple.metal",parent,backup.fileSystemRepresentation))
            [failures addObject:[@"Cannot preserve shader cache: " stringByAppendingString:entry.path]];
        else [moved addObject:@{@"Original":entry.path,@"Backup":[entry.URLByDeletingLastPathComponent URLByAppendingPathComponent:backup].path}];
        close(parent);
    }
    close(base);
    return @{@"Quarantined":moved,@"Errors":failures};
}
