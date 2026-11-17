#import <Foundation/Foundation.h>

// Read Apple's signed firmlink directory payload. Each value is the
// original [uid, gid, mode] for a System-side directory.
NSDictionary<NSString *,NSArray<NSNumber *> *> *VIMBigSurFirmlinkMetadata(NSURL *zip,NSError **error);

// The signed directory payload/xattrs, not every optional entry in the generic
// text map, determine the real System stubs. No filesystem writes occur here.
NSDictionary *VIMBigSurFirmlinkPlan(NSURL *zip,NSString *systemMap,NSError **error);

// Reserve matching inode and xattr slots on the unpublished System volume.
// The caller uses the APFS finalizer only after detaching the scratch disk.
NSDictionary *VIMBigSurReserveFirmlinks(NSURL *zip,NSURL *system,NSError **error);
