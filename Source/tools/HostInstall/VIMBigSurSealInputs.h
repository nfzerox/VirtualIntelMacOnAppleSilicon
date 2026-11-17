#import <Foundation/Foundation.h>
#include "../host-install-pbzx.h"

// Decode the original Intel IMG4/PBZE mtree from Preboot to digest.db and
// mtree.txt in a fresh, private job directory. No decoded archive is kept.
BOOL VIMBigSurExtractSealInputs(NSURL *intelMTree,NSURL *directory,VIMArchiveProgress progress,void *context,NSError **error);
