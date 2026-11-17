#import <Foundation/Foundation.h>

// Sort generated filesystem records and merge the original digest stream.
// The caller owns scratch storage behind sink; partial output is not sealed.
NSDictionary *VIMBuildBigSurTreeWithDigests(NSMutableArray<NSArray<NSData *> *> *records,
    NSURL *database,NSData *rootHash,BOOL (^sink)(uint64_t,NSData *),
    BOOL (^cancelled)(void),NSError **error);

// Build and read back a signed-root-matching fsroot block file from the
// detached System and original signed media. Returns the extent-ID plan
// with the authenticated tree's node and record counts.
NSDictionary *VIMBuildBigSurSealedFilesystem(NSURL *raw,uint64_t offset,uint64_t size,NSUUID *system,
    NSURL *mtree,NSURL *database,NSURL *zip,NSData *rootHash,NSURL *output,
    BOOL (^cancelled)(void),NSError **error);

// Independently follow every child from the signed hash. The reader can
// supply either the relative scratch file or a placed APFS object map.
BOOL VIMVerifyBigSurTreeNodes(uint64_t count,NSData *rootHash,NSData * (^readBlock)(uint64_t),
    BOOL (^cancelled)(void));

// Read leaf records only after their containing block authenticates.
typedef BOOL (^VIMBigSurSealRecord)(const uint8_t *key,size_t keyLength,const uint8_t *value,size_t valueLength);
BOOL VIMVisitBigSurAuthenticatedTree(uint64_t count,NSData *rootHash,NSData * (^readBlock)(uint64_t),
    VIMBigSurSealRecord record,BOOL (^cancelled)(void));
