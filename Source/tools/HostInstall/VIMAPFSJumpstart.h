#import <Foundation/Foundation.h>

BOOL VIMIsIntelAPFSDriver(NSData *driver);

// Embed the original Intel Recovery EFI driver in a new, unpublished raw
// APFS disk. The caller must have detached every mount of the scratch image.
// Never pass a device, reference VM disk, or already published guest image.
BOOL VIMPublishAPFSJumpstart(NSURL *raw,NSData *driver,NSError **error);
