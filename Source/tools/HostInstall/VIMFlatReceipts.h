#import <Foundation/Foundation.h>

// Apple's Darwin 9 PackageKit database (version 17). Each package supplies
// its original ID, version, groups and BOM ownership/mode records.
void VIMWriteLeopardReceipts(NSURL *database, NSArray<NSDictionary *> *packages,
                            NSArray<NSString *> *aclPaths, NSString *acl);
// Add OS-update packages without discarding the original installation records.
void VIMAppendLeopardReceipts(NSURL *database, NSArray<NSDictionary *> *packages);
