#import <Foundation/Foundation.h>

// Verify and stage a Tiger-era Apple Software Update tarball. The RSA key
// must come from the authenticated base system's SoftwareUpdate framework,
// never from the update tarball or the remote catalog. The destination is a
// new file in the private install attempt; only a successful return makes
// it eligible for extraction.
BOOL VIMStageApplePeriodUpdate(NSURL *source, NSString *packageMember, NSData *publicKey,
    uint64_t maxPackageBytes, NSURL *destination, BOOL (^cancelled)(void), NSError **error);
