#import "VIMAppleDouble.h"
#import "VIMGuestPath.h"
#include <errno.h>
#include <sys/stat.h>

void VIMNormalizeAppleDoubleMetadata(NSURL *root,
    NSMutableDictionary<NSString *,NSArray<NSNumber *> *> *metadata) {
    for(NSString *path in metadata.allKeys) {
        NSString *leaf=path.lastPathComponent;
        NSArray<NSNumber *> *entry=metadata[path];
        if(leaf.length<=2||![leaf hasPrefix:@"._"]||!S_ISREG(entry[2].unsignedShortValue))continue;
        NSURL *sidecar=VIMResolveGuestPath(root,path,NO,NULL);struct stat info;
        if(!sidecar||!lstat(sidecar.fileSystemRepresentation,&info)||errno!=ENOENT)continue;
        NSString *data=[path.stringByDeletingLastPathComponent stringByAppendingPathComponent:[leaf substringFromIndex:2]];
        NSURL *file=VIMResolveGuestPath(root,data,NO,NULL);
        if(!file||lstat(file.fileSystemRepresentation,&info))continue;
        if(!metadata[data]) {
            NSMutableArray<NSNumber *> *identity=[entry mutableCopy];
            identity[2]=@(info.st_mode);
            metadata[data]=identity;
        }
        [metadata removeObjectForKey:path];
    }
}
