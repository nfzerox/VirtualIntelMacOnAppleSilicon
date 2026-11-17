#import <Foundation/Foundation.h>

typedef struct {uint64_t stream,offset,length,physical;} VIMBigSurExtent;

// Sort in place, reject overlapping records, and serialize fixed-width
// [stream, offset, length, physical] rows in little-endian order.
NSData *VIMBigSurEncodeExtentRecords(NSMutableData *extents);

// Fixed 16-byte keys and 16-byte leaf values. The returned address is the
// last block of a contiguous run. Used by the FEXT and object-map trees.
uint64_t VIMBigSurPhysicalTreeBlockCount(uint64_t rows);
uint64_t VIMBigSurWritePhysicalTree(NSData *rows,uint64_t start,uint64_t xid,uint32_t subtype,
    BOOL (^sink)(uint64_t address,NSData *block));

// Single-level physical tree for the snapshot OMAP table (8-byte key).
NSData *VIMBigSurSnapshotMapBlock(uint64_t address,uint64_t xid);
