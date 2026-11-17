#import <Foundation/Foundation.h>
#include "../host-install-pbzx.h"

typedef struct {
    uint32_t uid,gid,flags;
    uint16_t mode;
    BOOL compressed;
    uint32_t compressionType;
    uint64_t resourceLength;
} VIMBigSurFixup;

// A compact alias row supplies its owner's path and inode metadata. Return
// NO to stop; the visitor runs serially on the decoding consumer.
typedef BOOL (^VIMBigSurFixupVisitor)(NSString *path,VIMBigSurFixup metadata,NSString *hardLinkOwner);
BOOL VIMBigSurVisitFixups(NSURL *zip,VIMBigSurFixupVisitor visit,
    VIMArchiveProgress progress,void *context,NSError **error);

// Materialize a signed alias on a private staging volume. Do not traverse
// a symlink, overwrite an existing file, or copy the original file's data.
BOOL VIMBigSurStageFixupHardLink(int root,NSString *owner,NSString *alias);
// Remove only a pre-existing name for the same inode before Apple Archive
// replaces the canonical file during compression. Missing aliases are fine.
BOOL VIMBigSurDetachFixupHardLink(int root,NSString *owner,NSString *alias);

// Read FAST_PROMOTE paths and original rootless/alternate-link bytes from both
// signed metadata members. This does not write to any mounted filesystem.
NSDictionary *VIMBigSurReadSealPolicy(NSURL *zip,NSError **error);

// Compression repair may clear rejected protected xattrs on regular files.
// Restore the original template's protection slots on the private Data copy.
BOOL VIMBigSurStageDataProtection(NSURL *zip,NSURL *data,NSError **error);
