#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <dispatch/dispatch.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include "../compat/PVGAIRTarget.h"
#define PVG_REPAIR_LEGACY_AIR 1
#include "../compat/PVGMetalCompat/PVGShaderCache.h"

int main(void) {
    @autoreleasepool {
        char directory[] = "/tmp/pvg-cache-storage-XXXXXX";
        if (!mkdtemp(directory)) return 1;
        NSString *root = @(directory);
        NSString *path = [root stringByAppendingPathComponent:@"entry"];
        NSData *payload = [@"test metallib payload" dataUsingEncoding:NSUTF8StringEncoding];
        pvgWriteShaderCache(path, payload);
        if (![pvgReadShaderCache(path) isEqualToData:payload]) return 2;
        int fd = open(path.fileSystemRepresentation, O_WRONLY);
        if (fd < 0 || pwrite(fd, "!", 1, 0) != 1) return 3;
        close(fd);
        if (pvgReadShaderCache(path)) return 4;
        pvgWriteShaderCache(path, payload);
        if (![pvgReadShaderCache(path) isEqualToData:payload]) return 5;
        NSString *link = [root stringByAppendingPathComponent:@"symlink"];
        if (symlink(path.fileSystemRepresentation, link.fileSystemRepresentation)) return 6;
        if (pvgReadShaderCache(link)) return 7;
        unlink(link.fileSystemRepresentation);
        NSString *full = [root stringByAppendingPathComponent:@"sparse-quota"];
        fd = open(full.fileSystemRepresentation, O_CREAT | O_WRONLY | O_EXCL, 0600);
        if (fd < 0 || ftruncate(fd, 128 * 1024 * 1024)) return 8;
        close(fd);
        NSString *rejected = [root stringByAppendingPathComponent:@"quota-rejected"];
        pvgWriteShaderCache(rejected, payload);
        if ([[NSFileManager defaultManager] fileExistsAtPath:rejected]) return 9;
        unlink(full.fileSystemRepresentation);
        unlink(path.fileSystemRepresentation);
        unlink([[root stringByAppendingPathComponent:@".writer-lock"] fileSystemRepresentation]);
        rmdir(directory);
        puts("PASS: roundtrip, corrupt miss, repair, symlink rejection, quota admission");
    }
    return 0;
}
