#import "VIMBigSurSealTree.h"

typedef BOOL (^VIMBigSurExtentVisitor)(uint64_t stream,uint64_t logical,uint64_t length,uint64_t physical);

// Authenticate the actual bytes of both Intel dyld caches through the
// published snapshot's extent tree, not through the host's mounted VFS.
BOOL VIMVerifyBigSurIntelCaches(NSURL *mtree,uint64_t blocks,
    BOOL (^visitTree)(VIMBigSurSealRecord),BOOL (^visitExtents)(VIMBigSurExtentVisitor),
    BOOL (^read)(uint64_t position,void *buffer,size_t length),BOOL (^cancelled)(void),NSError **error);

// A matching metadata seal alone does not authenticate external file bytes.
// Check every signed data stream before publishing the new System disk.
BOOL VIMVerifyBigSurSignedFileData(uint64_t blocks,
    BOOL (^visitTree)(VIMBigSurSealRecord),BOOL (^visitExtents)(VIMBigSurExtentVisitor),
    BOOL (^read)(uint64_t position,void *buffer,size_t length),BOOL (^cancelled)(void),NSError **error);
