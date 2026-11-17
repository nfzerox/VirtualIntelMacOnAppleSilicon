#import <Foundation/Foundation.h>
@class VIMAPFSCheckpoint;

// Combine a detached allocation with delayed retirement of the replaced
// blocks. Retired main-store ranges (if any) are [start, length]. Returns
// ordered [Block, Bytes] records with the anchor last; never writes itself.
NSArray<NSDictionary *> *VIMAPFSRetirementWrites(VIMAPFSCheckpoint *checkpoint,NSDictionary *allocation,
    NSArray<NSArray<NSNumber *> *> *retiredMain,NSData *superblock,NSError **error);
