#import <Foundation/Foundation.h>
#include "../host-install-pbzx.h"

typedef NS_ENUM(NSInteger,VIMAssistantCacheLayout) {
    VIMAssistantCachesInline=1,
    VIMAssistantCachesIntelCryptex,
};

// Read the original Apple AssetData ZIP, mounted read-only from an already
// authenticated installer. These routines never extract to archive paths.
NSArray<NSString *> *VIMBigSurArchiveMembers(NSURL *zip, NSError **error);
NSData *VIMBigSurReadSmallArchiveMember(NSURL *zip, NSString *member, NSError **error);
// Copy explicitly planned regular ZIP members into an unpublished disk.
BOOL VIMCopyOriginalBootMembers(NSURL *zip, NSDictionary<NSString *,NSURL *> *destinations,
    BOOL (^cancelled)(void), NSError **error);

// Cross-check the mounted package manifest and return the System PBZX
// member sequence with Apple's target-space hints (not encoded lengths).
NSDictionary *VIMBigSurPayloadInventory(NSURL *zip, NSDictionary *signedAsset, NSError **error);
NSDictionary *VIMAssistantPayloadInventory(NSURL *zip, NSDictionary *signedAsset, NSInteger major, NSError **error);
NSDictionary *VIMAssistantAssetForArchive(NSArray *assets, NSURL *archive, NSInteger major, NSError **error);

// Decode one planned PBZX member to a borrowed Apple Archive consumer.
BOOL VIMBigSurDecodePayloadMember(NSURL *zip, NSString *member, int output,
    VIMArchiveProgress progress, void *context, NSError **error);

BOOL VIMBigSurDecodeFixupManifest(NSURL *zip,int output,VIMArchiveProgress progress,void *context,NSError **error);

// Use the host's Apple Archive tool for the original decmpfs encoding.
// Accept its "repaired" exit status provisionally; callers must check the
// detached System against the original compression manifest. Progress counts
// decoded manifest bytes accepted by the backpressured Apple Archive pipe.
BOOL VIMBigSurRestoreCompression(NSURL *zip,NSURL *system,BOOL (^cancelled)(void),
    void (^progress)(uint64_t sent,uint64_t total),NSError **error);
BOOL VIMBigSurRepairCompression(NSURL *zip,NSURL *system,NSURL *paths,BOOL (^cancelled)(void),NSError **error);
// Release APFS allocation past the end of a compressed resource on the
// unpublished host-mounted System, preserving the original resource bytes.
BOOL VIMBigSurTrimCompressedFork(NSURL *file,NSError **error);

// Extract into the caller's unpublished staging System volume. Host-side
// ownership and protected flags are deferred to detached finalization.
BOOL VIMBigSurExtractPayloadMember(NSURL *zip, NSString *member, NSURL *root,
    VIMArchiveProgress progress, void *context, NSError **error);

// Apply all original System chunks in manifest order. Progress counts bytes
// of the ZIP's PBZX members against the inventory's TotalEncodedLength.
BOOL VIMBigSurStageSystemPayload(NSURL *zip, NSDictionary *inventory, NSURL *root,VIMAssistantCacheLayout layout,
    VIMArchiveProgress progress, void *context, NSError **error);

// Stage one unpublished Preboot/Recovery pair under this Data UUID and
// return the expected Intel seal. `system` is the staged original System
// payload, not Recovery. No snapshot is selected here. On error the caller
// discards the unpublished attempt (including any incomplete files).
NSData *VIMBigSurStageBootAssets(NSURL *zip, NSURL *system, NSURL *preboot, NSURL *recovery,
    NSUUID *dataUUID, NSString *build, BOOL (^cancelled)(void), NSError **error);
