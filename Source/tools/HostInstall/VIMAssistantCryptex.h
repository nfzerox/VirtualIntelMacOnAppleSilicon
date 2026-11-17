#import <Foundation/Foundation.h>

// Plan the original Intel cryptex1/current tree from the original, authenticated
// installer members and BuildManifest. Does not mount or write a disk.
NSDictionary *VIMAssistantIntelCryptexPlan(NSArray<NSString *> *members,NSDictionary *manifest,NSError **error);
// Stage the planned tree only on the unpublished mounted APFS Preboot volume.
// Use a private empty directory on the library drive for temporary patches.
BOOL VIMStageAssistantIntelCryptex(NSURL *zip,NSURL *preboot,NSURL *scratch,
    NSUUID *dataUUID,NSString *build,NSURL *decoder,BOOL (^cancelled)(void),NSError **error);
// The native helper runs inside Apple's unchanged installer framework layout.
NS_INLINE NSURL *VIMAssistantCryptexDecoderLibrary(NSURL *decoder) {
    return [decoder.URLByDeletingLastPathComponent.URLByDeletingLastPathComponent
        URLByAppendingPathComponent:@"Frameworks/OSInstallerSetupInternal.framework/Versions/A/OSInstallerSetupInternal"];
}
// Read the caches from the original, decoded Cryptex mounted read-only.
BOOL VIMVerifyAssistantIntelCryptexCaches(NSURL *mountedImage,NSError **error);
