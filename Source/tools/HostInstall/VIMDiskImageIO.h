#import <Foundation/Foundation.h>

// Detached installation disk bytes. A sparsebundle has the same address
// space as a regular raw image; absent bands/tails read as zero. This keeps
// APFS finalization independent of the library's host filesystem.
@interface VIMDiskImageIO : NSObject
@property(nonatomic,readonly) uint64_t size;
- (instancetype)initWithURL:(NSURL *)url writable:(BOOL)writable error:(NSError **)error;
- (BOOL)readAt:(uint64_t)offset bytes:(void *)bytes length:(size_t)length;
- (BOOL)writeAt:(uint64_t)offset bytes:(const void *)bytes length:(size_t)length;
- (BOOL)synchronize;
@end
