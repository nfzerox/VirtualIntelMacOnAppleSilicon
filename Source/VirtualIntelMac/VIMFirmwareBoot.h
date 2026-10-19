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
