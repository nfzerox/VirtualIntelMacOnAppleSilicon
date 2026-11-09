#import <Foundation/Foundation.h>

typedef NS_ENUM(NSInteger,VIMFirmwareBootTarget) {
    VIMFirmwareBootUnknown,
    VIMFirmwareBootInstalledSystem,
    VIMFirmwareBootInstaller,
};

/// Read-only EDK II variable-store inspection; never rewrites guest firmware.
/// Unknown/corrupt formats are not evidence of an installer boot.
FOUNDATION_EXPORT VIMFirmwareBootTarget VIMFirmwareBootTargetFromData(NSData *data);
FOUNDATION_EXPORT VIMFirmwareBootTarget VIMFirmwareBootTargetAtURL(NSURL *url);
/// YES when the Apple variable store holds startosinstall's staged-install
/// handoff (install-product-url, or Big Sur's IASCurrentInstallPhase), i.e. the next boot should be the staged
/// installer on the target disk rather than installer media.
FOUNDATION_EXPORT BOOL VIMFirmwareHasStagedInstallAtURL(NSURL *url);
