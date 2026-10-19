// Per-user converted-AIR cache. Qualified/default on Mojave; other targets
// remain opt-in. Never caches a pipeline or a device-owned object.
#include <CommonCrypto/CommonDigest.h>
#include <sys/stat.h>
#include <sys/file.h>

static NSString *pvgShaderCachePath(const void *bytes, size_t length,
                                    NSString *name, NSUInteger stage) {
    static NSString *directory;
    static NSData *recipe;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        const char *setting = getenv("PVG_AIR_CACHE_DIR");
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 101500
        if (!setting) setting = "auto";
#endif
        if (setting && !strcmp(setting, "0")) return;
        if (!setting || !*setting) return;
        NSString *path = [NSString stringWithUTF8String:setting];
        if ([path isEqualToString:@"auto"]) {
            NSString *base = NSSearchPathForDirectoriesInDomains(
                NSCachesDirectory, NSUserDomainMask, YES).firstObject;
            if (!base) return;
            if (![[NSFileManager defaultManager] createDirectoryAtPath:base
                withIntermediateDirectories:YES
                attributes:@{NSFilePosixPermissions: @0700} error:NULL]) return;
            path = [base stringByAppendingPathComponent:@"org.pvgbackport.air-v1"];
        }
        if (![path isAbsolutePath]) return;
        if (mkdir(path.fileSystemRepresentation, 0700) && errno != EEXIST)
            return;
        struct stat st;
        if (lstat(path.fileSystemRepresentation, &st) || !S_ISDIR(st.st_mode) ||
            st.st_uid != geteuid() || (st.st_mode & 0077)) return;
        // Invalidate for every rebuilt adapter and donor-library replacement.
        // Enumerate metadata only: do not read hundreds of MB of LLVM at launch.
        NSMutableData *identity = [NSMutableData data];
        const char version[] = "PVG-AIR-cache-v1:" __DATE__ ":" __TIME__ ":"
            PVG_AIR_TARGET;
        [identity appendBytes:version length:sizeof(version)];
        unsigned repair = PVG_REPAIR_LEGACY_AIR;
        [identity appendBytes:&repair length:sizeof(repair)];
        NSArray *roots = @[
            @"/System/Library/PrivateFrameworks/GPUCompiler.framework/Versions/31001/Libraries",
            @"/System/Library/Extensions/AppleParavirtGPUMetal.bundle/Contents/Resources"];
        for (NSString *root in roots) {
            NSArray *children = [[[NSFileManager defaultManager]
                subpathsAtPath:root] sortedArrayUsingSelector:@selector(compare:)];
            if (!children.count) return;
            for (NSString *child in children) {
                NSString *file = [root stringByAppendingPathComponent:child];
                if (stat(file.fileSystemRepresentation, &st)) return;
                if (!S_ISREG(st.st_mode)) continue;
                NSData *label = [file dataUsingEncoding:NSUTF8StringEncoding];
                uint64_t fields[] = {label.length, st.st_dev, st.st_ino,
                    st.st_size, st.st_mtimespec.tv_sec, st.st_mtimespec.tv_nsec,
                    st.st_ctimespec.tv_sec, st.st_ctimespec.tv_nsec};
                [identity appendBytes:fields length:sizeof(fields)];
                [identity appendData:label];
            }
        }
        directory = [path copy];
        recipe = [identity copy];
    });
    if (!directory || length > UINT32_MAX) return nil;
    CC_SHA256_CTX context;
    CC_SHA256_Init(&context);
    CC_SHA256_Update(&context, recipe.bytes, (CC_LONG)recipe.length);
    CC_SHA256_Update(&context, &stage, sizeof(stage));
    NSData *functionName = [name dataUsingEncoding:NSUTF8StringEncoding];
    uint64_t nameLength = functionName.length;
    CC_SHA256_Update(&context, &nameLength, sizeof(nameLength));
    CC_SHA256_Update(&context, functionName.bytes, (CC_LONG)nameLength);
    CC_SHA256_Update(&context, bytes, (CC_LONG)length);
    unsigned char hash[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(hash, &context);
    NSMutableString *leaf = [NSMutableString stringWithCapacity:64];
    for (unsigned i = 0; i < sizeof(hash); ++i)
        [leaf appendFormat:@"%02x", hash[i]];
    return [directory stringByAppendingPathComponent:leaf];
}

static NSData *pvgReadShaderCache(NSString *path) {
    if (!path) return nil;
    int fd = open(path.fileSystemRepresentation, O_RDONLY | O_NOFOLLOW);
    if (fd < 0) return nil;
    struct stat st;
    NSData *result = nil;
    if (!fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_uid == geteuid() &&
        !(st.st_mode & 0022) && st.st_size > CC_SHA256_DIGEST_LENGTH &&
        st.st_size <= 32 * 1024 * 1024) {
        NSMutableData *data = [NSMutableData dataWithLength:(NSUInteger)st.st_size];
        size_t done = 0;
        while (done < data.length) {
            ssize_t n = read(fd, (char *)data.mutableBytes + done, data.length - done);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) break;
            done += n;
        }
        if (done == data.length) {
            NSUInteger size = data.length - CC_SHA256_DIGEST_LENGTH;
            unsigned char digest[CC_SHA256_DIGEST_LENGTH];
            CC_SHA256(data.bytes, (CC_LONG)size, digest);
            if (!memcmp(digest, (char *)data.bytes + size, sizeof(digest)))
                result = [data subdataWithRange:NSMakeRange(0, size)];
        }
    }
    close(fd);
    return result;
}

static void pvgWriteShaderCache(NSString *path, NSData *compiled) {
    if (!path || !compiled.length || compiled.length > 32 * 1024 * 1024 - 32)
        return;
    // Admission control only: never evict files on an application's launch
    // path. Cooperating writers share a nonblocking lock; contention misses.
    NSString *directory = [path stringByDeletingLastPathComponent];
    NSString *lockPath = [directory stringByAppendingPathComponent:@".writer-lock"];
    int lock = open(lockPath.fileSystemRepresentation,
                    O_RDWR | O_CREAT | O_NOFOLLOW, 0600);
    if (lock < 0) return;
    struct stat st;
    if (fstat(lock, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
        (st.st_mode & 0077) || flock(lock, LOCK_EX | LOCK_NB)) {
        close(lock);
        return;
    }
    NSArray *entries = [[NSFileManager defaultManager]
        contentsOfDirectoryAtPath:directory error:NULL];
    uint64_t total = 0;
    BOOL admissible = entries != nil && entries.count < 1024;
    for (NSString *entry in entries) {
        NSString *file = [directory stringByAppendingPathComponent:entry];
        if (lstat(file.fileSystemRepresentation, &st) || !S_ISREG(st.st_mode) ||
            st.st_size < 0) { admissible = NO; break; }
        total += (uint64_t)st.st_size;
        if (total > 128 * 1024 * 1024) { admissible = NO; break; }
    }
    if (!admissible || total + compiled.length + 32 > 128 * 1024 * 1024) {
        close(lock);
        return;
    }
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(compiled.bytes, (CC_LONG)compiled.length, digest);
    NSMutableData *data = [NSMutableData dataWithData:compiled];
    [data appendBytes:digest length:sizeof(digest)];
    // Atomic publication permits concurrent processes; incomplete entries miss.
    [data writeToFile:path options:NSDataWritingAtomic error:NULL];
    close(lock);
}

static id pvgFunctionFromCompiled(id device, id function, NSData *compiled) {
    void *copy = malloc(compiled.length);
    if (!copy) return nil;
    memcpy(copy, compiled.bytes, compiled.length);
    dispatch_data_t data = dispatch_data_create(copy, compiled.length,
        dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0),
        DISPATCH_DATA_DESTRUCTOR_FREE);
    id<MTLLibrary> library = [device newLibraryWithData:data error:NULL];
    dispatch_release(data);
    id result = [library newFunctionWithName:[function name]];
    [library release];
    return [result autorelease];
}
