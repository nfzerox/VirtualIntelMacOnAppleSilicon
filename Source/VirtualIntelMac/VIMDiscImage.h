#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN

// Validate the container, not just the filename. DMGs use QEMU's read-only DMG
// block driver directly; compressed containers must never be opened as raw.
BOOL VIMValidateOpticalImage(NSString *path, NSError *_Nullable *_Nullable error);
NSString *_Nullable VIMOpticalImageFormat(NSString *path,NSError *_Nullable *_Nullable error);
/// Run the same block driver outside the VM before touching its current media.
/// Completion is main-thread; no mount, copying, conversion or guest I/O.
void VIMCheckOpticalImage(NSString *path,NSString *format,NSString *qemuImageTool,
                         void (^completion)(NSError *_Nullable));
NSError *VIMFriendlyDiscImageError(NSString *path,NSError *underlying);
NS_ASSUME_NONNULL_END
