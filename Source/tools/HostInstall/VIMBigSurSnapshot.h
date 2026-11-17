#import <Foundation/Foundation.h>

// Plan the System snapshot on an unpublished, detached APFS disk. The
// plan owns its new contiguous blocks and ordered checkpoint writes. No
// file is modified here; its last checkpoint write anchors the new xid.
NSDictionary *VIMPlanBigSurSnapshot(NSURL *raw,NSUUID *system,NSUUID *snapshot,
    NSURL *authenticatedTree,uint64_t nodeCount,NSData *rootHash,
    NSDictionary<NSNumber *,NSNumber *> *ids,BOOL (^cancelled)(void),NSError **error);

// Apply only to a newly generated raw image that the caller has detached.
// Commit the ring/anchor after the new blocks, then reopen and authenticate
// the snapshot through the on-disk maps before returning success.
BOOL VIMPublishBigSurSnapshot(NSURL *raw,NSUUID *system,NSUUID *snapshot,
    NSURL *authenticatedTree,uint64_t nodeCount,NSData *rootHash,NSURL *mtree,
    NSDictionary<NSNumber *,NSNumber *> *ids,BOOL (^cancelled)(void),NSError **error);
