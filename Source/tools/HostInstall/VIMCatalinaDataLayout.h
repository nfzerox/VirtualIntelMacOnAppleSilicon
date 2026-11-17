#import <Foundation/Foundation.h>

// Logical paths have no leading slash. Values are Data-relative paths
// from the original Catalina Core payload's /usr/share/firmlinks.
NSDictionary<NSString *,NSString *> *VIMParseCatalinaFirmlinks(NSString *text,NSError **error);
// Parse the complete map, including optional paths not materialized on a
// retail sealed System. The signed firmlink payload selects the active set.
NSDictionary<NSString *,NSString *> *VIMParseSystemFirmlinks(NSString *text,NSError **error);
NSString *VIMCatalinaDataPath(NSString *logical,NSDictionary<NSString *,NSString *> *links);

// Make Apple's writable Data template visible inside the unpublished
// combined staging tree before provisioning the account and graphics.
// Copy must preserve links and extended metadata (the worker uses ditto).
BOOL VIMMaterializeCatalinaDataTemplate(NSURL *root,NSDictionary<NSString *,NSString *> *links,
    NSMutableDictionary<NSString *,NSArray<NSNumber *> *> *metadata,
    BOOL (^copy)(NSURL *source,NSURL *destination,NSError **error),NSError **error);

// Project the combined tree onto empty writable scratch mounts. Returns
// SystemPermissions, DataPermissions and FirmlinkInodes keyed by the new
// APFS inode numbers. The detached finalizer applies them after unmount.
NSDictionary *VIMProjectCatalinaVolumes(NSURL *source,NSURL *system,NSURL *data,
    NSDictionary<NSString *,NSString *> *links,NSDictionary<NSString *,NSArray<NSNumber *> *> *metadata,
    BOOL (^copy)(NSURL *source,NSURL *destination,NSError **error),NSError **error);
