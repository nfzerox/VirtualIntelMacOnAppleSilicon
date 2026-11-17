#import <Foundation/Foundation.h>

// Ownership/mode and blessing for a clean, detached scratch HFS+ image.
// Keys are CNIDs from the mounted volume; values are [uid, gid, mode],
// optionally followed by an HFS+ BSD adminFlags bitmask to add after detach.
BOOL VIMFinalizeHFSCatalog(NSURL *image, uint64_t offset, uint64_t size,
                          NSDictionary<NSNumber *, NSArray<NSNumber *> *> *permissions,
                          uint32_t systemFolder, uint32_t bootFile, NSError **error);

enum { VIMHFSRecoverySectors = 1269536 };
// Add a detached, blessed HFS+ Recovery volume in the reserved GPT gap.
BOOL VIMAddHFSRecovery(NSURL *image, NSURL *recovery, NSError **error);

// Publish the APFS type in both GPT copies after Apple's period converter
// has converted the staging volume. The supplied EFI driver is from the
// original install media. The scratch image must be detached.
BOOL VIMPublishConvertedAPFSPartition(NSURL *image, NSData *efiDriver, NSError **error);

// Locate the APFS partition in a detached bundle or raw disk with agreeing GPT copies.
BOOL VIMGetAPFSPartition(NSURL *image,uint64_t *offset,uint64_t *length,NSError **error);

// Read the Intel boot-service driver from a detached original Recovery GPT.
NSData *VIMReadIntelAPFSJumpstart(NSURL *rawRecovery,NSError **error);

// Copy a detached bundle or one of its slices into a new, sparse regular
// file on a scratch APFS volume. Never ask the host to open the guest disk.
BOOL VIMExportSparseBundleRange(NSURL *image, uint64_t offset, uint64_t length,
                                NSURL *raw, NSError **error);

// Grow a new, detached GPT/HFS staging seed on APFS using sparse file growth.
// Preserves the formatted EFI partition and expands the second GPT partition;
// the caller immediately replaces that empty HFS filesystem with APFS.
// Not an installed-filesystem resize operation.
BOOL VIMExtendSparseInstallSeed(NSURL *raw, uint64_t bytes, NSError **error);
