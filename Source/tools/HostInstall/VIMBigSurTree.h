#import <Foundation/Foundation.h>

// APFS filesystem order is by object, record type, then the record's
// secondary key. The original digest database is already in this order.
NSComparisonResult VIMCompareBigSurRecordKeys(NSData *a,NSData *b);

// Write authenticated fsroot blocks into unpublished scratch storage.
// Relative IDs start at the root (0); sink calls need not be ordered.
@interface VIMBigSurTreeBuilder : NSObject
- (instancetype)initWithSink:(BOOL (^)(uint64_t relative,NSData *block))sink;
- (BOOL)appendKey:(NSData *)key value:(NSData *)value;
- (NSDictionary *)finishWithRootHash:(NSData *)expected error:(NSError **)error;
@end
