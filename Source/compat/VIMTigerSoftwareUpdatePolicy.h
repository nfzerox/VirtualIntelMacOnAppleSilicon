#pragma once
#import <Foundation/Foundation.h>

/* Software Update labels, as softwareupdate -l prints them on the iMac7,1
 * Tiger restore, whose packages replace files the Tiger graphics support
 * depends on. "iMac Software Update" 1.2.1 (catalog 061-4083) replaces the
 * restore's OpenGL.framework and Rosetta GL pieces and removes the app's
 * GLEngine bundle contents; the guest then stops at the boot progress
 * indicator. Tiger keeps ignored labels machine-wide in
 * /Library/Preferences/com.apple.SoftwareUpdate.plist InactiveUpdates, the
 * key its Ignore Update command and softwareupdate --ignore write. */
static inline NSArray<NSString *> *VIMTigerIgnoredSoftwareUpdates(void) {
    return @[@"iMacSoftwareUpdate1.2.1-1.2.1"];
}

static NSString *const VIMTigerSoftwareUpdatePreferences=@"Library/Preferences/com.apple.SoftwareUpdate.plist";
