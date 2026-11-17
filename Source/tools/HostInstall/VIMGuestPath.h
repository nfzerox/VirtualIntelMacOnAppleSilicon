#import <Foundation/Foundation.h>

// Resolve guest aliases inside an offline volume. An absolute symlink is
// relative to that volume, never the host's /. The final component is not
// followed when removing an obsolete file or symlink.
NSURL *VIMResolveGuestPath(NSURL *root, NSString *path, BOOL followFinal, NSError **error);
