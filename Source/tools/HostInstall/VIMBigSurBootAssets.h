#import <Foundation/Foundation.h>

#define VIMIntelRecoveryPatchMember @"AssetData/payloadv2/basesystem_patches/x86_64BaseSystem.dmg"

// Maps exact members of the authenticated AssetData ZIP to paths below the
// Data UUID in Preboot and Recovery. It does not select a System snapshot.
NSDictionary *VIMBigSurBootAssetPlan(NSArray<NSString *> *members, NSError **error);

// Read the expected SHA-256 seal from the Intel IM4P xsys asset. The caller
// must establish provenance and verify the new snapshot independently.
NSData *VIMBigSurIntelRootHash(NSData *asset, NSError **error);
