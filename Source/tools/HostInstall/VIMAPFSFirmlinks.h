#import <Foundation/Foundation.h>

enum { VIMAPFSSystemRole=0x0001, VIMAPFSDataRole=0x0040 };

// Set the directory bit and rename each equal-length reserved xattr in
// Catalina's unpublished, detached System volume. The file must be a
// private regular raw image; neither a device nor a mounted guest is used.
// Destination data includes the trailing NUL of the Data-relative path.
BOOL VIMFinalizeAPFSFirmlinks(NSURL *raw,uint64_t containerOffset,uint64_t containerSize,
                              NSUUID *systemUUID,NSDictionary<NSNumber *,NSData *> *destinations,
                              NSError **error);

// Also restore ownership and mode of files projected onto a private APFS
// scratch volume without requiring root on the host. Keys are APFS inode
// numbers obtained while writing that volume; values are [uid, gid, mode].
// Restores equal-length rootless attribute slots retained by Apple Archive
// extraction, including those copied from System templates into Data.
BOOL VIMFinalizeAPFSVolume(NSURL *raw,uint64_t containerOffset,uint64_t containerSize,
                           NSUUID *volumeUUID,uint16_t role,
                           NSDictionary<NSNumber *,NSData *> *destinations,
                           NSDictionary<NSNumber *,NSArray<NSNumber *> *> *permissions,
                           NSError **error);

// Match signed Big Sur paths to detached APFS inode numbers and audit the
// decmpfs types and resource lengths and allocation before metadata edits.
// Permissions contains [uid, gid, mode, BSD flags] in Apple manifest order.
NSDictionary *VIMAuditBigSurSystemMetadata(NSURL *raw,uint64_t containerOffset,uint64_t containerSize,
    NSUUID *system,NSURL *zip,NSError **error);

// Restore signed System metadata and firmlinks only after the compression
// audit matches. The optional repair runs before any detached edit. Do not
// mount the image again once those edits begin.
BOOL VIMFinalizeBigSurSystemMetadata(NSURL *raw,uint64_t containerOffset,uint64_t containerSize,
    NSUUID *system,NSURL *zip,NSDictionary<NSNumber *,NSData *> *firmlinks,
    BOOL (^repair)(NSArray<NSString *> *paths,NSError **error),NSError **error);

// Map physical filesystem object identities to Apple's original Intel
// seal identities. ObjectIDs uses NSNumber keys; ChangeTime is in ns.
NSDictionary *VIMPlanBigSurCanonicalMap(NSURL *raw,uint64_t containerOffset,uint64_t containerSize,
    NSUUID *system,NSURL *mtree,NSError **error);

// Produce canonical key/value bytes from the detached System and signed
// media. Records are not sorted by canonical ID. Returns the ID plan for
// the separate extent tree; no source blocks are modified.
NSDictionary *VIMVisitBigSurCanonicalRecords(NSURL *raw,uint64_t containerOffset,uint64_t containerSize,
    NSUUID *system,NSURL *mtree,NSURL *zip,BOOL (^visit)(NSData *key,NSData *value),NSError **error);

// Preserve newly allocated physical data blocks while rekeying their
// extents to Apple’s signed canonical stream IDs.
NSData *VIMReadBigSurExtentRecords(NSURL *raw,uint64_t containerOffset,uint64_t containerSize,
    NSUUID *system,NSDictionary<NSNumber *,NSNumber *> *ids,NSError **error);

// Unsealed volume header, container OMAP rows not owned by this System,
// and exact blocks the new snapshot will retire. Never writes to the disk.
NSDictionary *VIMReadBigSurSnapshotSource(NSURL *raw,uint64_t containerOffset,uint64_t containerSize,
    NSUUID *system,NSError **error);

BOOL VIMVerifyBigSurPublishedSnapshot(NSURL *raw,uint64_t containerOffset,uint64_t containerSize,
    NSUUID *system,NSUUID *snapshot,uint64_t transaction,uint64_t nodes,NSData *rootHash,NSURL *mtree,
    BOOL (^cancelled)(void),NSError **error);
