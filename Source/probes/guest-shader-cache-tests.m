#import <Foundation/Foundation.h>
#include "guest-shader-cache.h"
int main(void){@autoreleasepool {
    NSURL *root=[NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString] isDirectory:YES];
    NSFileManager *fm=NSFileManager.defaultManager;
    NSArray *paths=@[@"aa/user/C/com.apple.metal",@"aa/user/C/com.apple.Chess/com.apple.metal",
                     @"aa/user/D/com.apple.metal",@"aa/user/C/ordinary",@"outside"];
    NSURL *folders=[root URLByAppendingPathComponent:@"private/var/folders"];
    for(NSString *path in paths) {
        NSURL *directory=[folders URLByAppendingPathComponent:path];
        NSCAssert([fm createDirectoryAtURL:directory withIntermediateDirectories:YES attributes:nil error:NULL],@"fixture directory");
        NSCAssert([[@"sentinel" dataUsingEncoding:NSUTF8StringEncoding] writeToURL:[directory URLByAppendingPathComponent:@"data"] atomically:YES],@"fixture bytes");
    }
    NSURL *link=[folders URLByAppendingPathComponent:@"bb/user/C/com.apple.metal"];
    [fm createDirectoryAtURL:link.URLByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:NULL];
    NSCAssert([fm createSymbolicLinkAtURL:link withDestinationURL:[folders URLByAppendingPathComponent:@"outside"] error:NULL],@"fixture symlink");
    NSDictionary *result=QuarantineGuestShaderCaches(root);
    NSCAssert([result[@"Errors"] count]==0&&[result[@"Quarantined"] count]==2,@"rename only two real Metal cache directories: %@",result);
    for(NSDictionary *record in result[@"Quarantined"]) {
        NSCAssert(![fm fileExistsAtPath:record[@"Original"]],@"old cache path must be absent");
        NSCAssert([[NSString stringWithContentsOfFile:[record[@"Backup"] stringByAppendingPathComponent:@"data"] encoding:NSUTF8StringEncoding error:NULL] isEqual:@"sentinel"],@"preserve cache bytes");
    }
    for(NSString *path in @[@"aa/user/D/com.apple.metal",@"aa/user/C/ordinary",@"outside"])
        NSCAssert([fm fileExistsAtPath:[[folders URLByAppendingPathComponent:path] URLByAppendingPathComponent:@"data"].path],@"unrelated content changed");
    NSCAssert([fm destinationOfSymbolicLinkAtPath:link.path error:NULL]!=nil,@"do not follow or replace symlinks");
    result=QuarantineGuestShaderCaches(root);
    NSCAssert([result[@"Errors"] count]==0&&[result[@"Quarantined"] count]==0,@"do not revisit backups");
    // Only this test's freshly created UUID fixture is removed.
    NSCAssert([fm removeItemAtURL:root error:NULL],@"fixture cleanup");
    puts("PASS shader cache preservation: shared/app caches, byte preservation, unrelated paths, symlinks, repeated scan");
}return 0;}
