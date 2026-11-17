#import <Foundation/Foundation.h>
@class VIMAPFSCheckpoint;

// Plan a contiguous allocation in the detached APFS container. The return
// includes Start, Writes (physical blocks), SpacemanOID and Spaceman, and
// RetiredInternal (old bitmap/CIB addresses that must be queued before
// committing the successor). This never writes to the disk.
NSDictionary *VIMPlanAPFSAllocation(VIMAPFSCheckpoint *checkpoint,uint64_t count,NSError **error);
