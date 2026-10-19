#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
/// File-publication layer only. Caller must validate OS/build, privilege,
/// signatures and target policy, then rebuild/verify boot caches before readiness.
/// Originals are renamed beside each target to remain on the same filesystem.
@interface VIMFileTransaction : NSObject
+ (nullable instancetype)createAtURL:(NSURL *)journal root:(NSURL *)root payload:(NSURL *)payload
                            targets:(NSArray<NSString *> *)targets metadata:(NSDictionary *)metadata error:(NSError **)error;
+ (nullable instancetype)loadURL:(NSURL *)journal error:(NSError **)error;
- (BOOL)publish:(NSError **)error;
- (BOOL)rollback:(NSError **)error;
- (BOOL)verifyPublished:(NSError **)error;
@property(nonatomic,readonly) NSDictionary *state;
@end
NS_ASSUME_NONNULL_END
