#import <Foundation/Foundation.h>

// Apple's OSBundleHelper property cache: gzip-compressed binary plist, with
// guest-absolute bundle paths. Pure metadata; no kext loading or host services.
NSData *VIMKextHelperCache(NSURL *root,NSString *architecture,BOOL includeLocalExtensions,NSError **error);
