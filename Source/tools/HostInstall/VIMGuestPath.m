#import "VIMGuestPath.h"
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>

NSURL *VIMResolveGuestPath(NSURL *root,NSString *path,BOOL followFinal,NSError **error) {
    NSMutableArray<NSString *> *pending=[path.pathComponents mutableCopy],*resolved=[NSMutableArray new];
    unsigned links=0;int failure=EINVAL;
    while(pending.count) {
        NSString *part=pending.firstObject;[pending removeObjectAtIndex:0];
        if([part isEqual:@"/"]||[part isEqual:@"."]||!part.length)continue;
        if([part isEqual:@".."]) {
            if(!resolved.count)goto fail;
            [resolved removeLastObject];continue;
        }
        NSURL *candidate=[root URLByAppendingPathComponent:[[resolved arrayByAddingObject:part] componentsJoinedByString:@"/"]];
        if(pending.count||followFinal) {
            struct stat info;
            if(lstat(candidate.fileSystemRepresentation,&info)){failure=errno;goto fail;}
            if(S_ISLNK(info.st_mode)) {
                if(++links>40){failure=ELOOP;goto fail;}
                char bytes[PATH_MAX+1];ssize_t size=readlink(candidate.fileSystemRepresentation,bytes,PATH_MAX);
                if(size<0){failure=errno;goto fail;}
                if(!size||size==PATH_MAX){failure=ENAMETOOLONG;goto fail;}
                bytes[size]=0;NSString *destination=[NSFileManager.defaultManager stringWithFileSystemRepresentation:bytes length:(NSUInteger)size];
                if(destination.isAbsolutePath)[resolved removeAllObjects];
                NSIndexSet *indexes=[NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0,destination.pathComponents.count)];
                [pending insertObjects:destination.pathComponents atIndexes:indexes];continue;
            }
            if(pending.count&&!S_ISDIR(info.st_mode)){failure=ENOTDIR;goto fail;}
        }
        [resolved addObject:part];
    }
    if(!resolved.count)goto fail;
    return [root URLByAppendingPathComponent:[resolved componentsJoinedByString:@"/"]];
fail:
    if(error)*error=[NSError errorWithDomain:NSPOSIXErrorDomain code:failure userInfo:nil];
    return nil;
}
