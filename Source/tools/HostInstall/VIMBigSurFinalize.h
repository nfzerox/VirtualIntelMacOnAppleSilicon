#import <Foundation/Foundation.h>
#import "VIMBigSurArchive.h"

// Finish the unpublished detached disk only. The group identities and inode
// records were collected while writing this disk, and the seal inputs and
// Intel EFI driver come from the original installer, not an installed VM.
NSDictionary *VIMFinalizeBigSurDisk(NSURL *raw,NSURL *zip,NSDictionary *group,
    NSDictionary *staged,NSURL *sealInputs,NSData *driver,NSURL *scratchTree,
    VIMAssistantCacheLayout cacheLayout,
    BOOL (^repair)(NSArray<NSString *> *paths,NSError **error),
    BOOL (^cancelled)(void),void (^progress)(NSString *stage),NSError **error);
