#import <Foundation/Foundation.h>

// Redirect the retired weak libdz dependency in a private host execution copy.
// No instruction offsets, guest changes, or host-library replacement involved.
BOOL VIMPrepareOfflineKextcache(NSURL *copy, BOOL *redirected, NSError **error);

// IOKit canonicalizes bundle URLs with realpath, but strips the volume-root
// prefix literally. Supply the same physical host path, including /private.
NSString *VIMOfflineKextcacheVolumePath(NSURL *root, NSError **error);
