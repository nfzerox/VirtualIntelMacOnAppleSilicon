#import <Foundation/Foundation.h>

// Read and plan 4 KiB, single-store APFS checkpoints on a detached raw
// scratch image. No operation in this class writes to the disk.
@interface VIMAPFSCheckpoint : NSObject
@property(nonatomic,readonly) NSData *superblock;
@property(nonatomic,readonly) uint64_t blockCount;
@property(nonatomic,readonly) uint64_t transactionID;
@property(nonatomic,readonly) uint64_t nextTransactionID;
- (instancetype)init NS_UNAVAILABLE;
- (instancetype)initWithRawImage:(NSURL *)image error:(NSError **)error;
- (NSData *)readBlock:(uint64_t)address;
- (NSData *)readEphemeral:(uint64_t)object volume:(uint64_t)volume;

// Keys are [volume OID, ephemeral OID]. Return ordered dictionaries with
// Block and Bytes keys. New ephemeral objects, map blocks and the next ring
// superblock precede the block-zero anchor. Never overwrite the active ring.
- (NSArray<NSDictionary *> *)successorWithObjects:(NSDictionary<NSArray<NSNumber *> *,NSData *> *)replacements
    discarding:(NSSet<NSArray<NSNumber *> *> *)removed superblock:(NSData *)superblock error:(NSError **)error;
@end
