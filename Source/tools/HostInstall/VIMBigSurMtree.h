#import <Foundation/Foundation.h>

typedef struct {
    uint64_t inode,externalStream,siblingID;
    uint8_t directoryType;
} VIMBigSurCanonicalEntry;

// Stream paths (raw UTF-8 bytes) and canonical identities from Apple's
// Intel seal manifest. This manifest uses a single canonical timestamp.
BOOL VIMBigSurVisitCanonicalTree(NSURL *mtree,
    BOOL (^visit)(NSData *path,VIMBigSurCanonicalEntry entry),uint64_t *timestamp,NSError **error);
