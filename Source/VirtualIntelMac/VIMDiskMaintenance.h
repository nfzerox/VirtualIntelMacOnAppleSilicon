#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
@interface VIMDiskMaintenance : NSObject
+ (BOOL)hasActiveOperation;
+ (BOOL)isMaintainingPackage:(NSURL *)URL;
/// Main-thread lifecycle and callbacks. Negative progress means indeterminate.
/// A successful result reports BeforeBytes, AfterBytes and Changed.
+ (BOOL)startForPackage:(NSURL *)URL identifier:(NSString *)identifier qemuImage:(NSURL *)qemuImage
    compressed:(BOOL)compressed progress:(void (^)(NSString *phase,double fraction))progress
    completion:(void (^)(NSDictionary *_Nullable result,NSError *_Nullable error))completion error:(NSError **)error;
@end
NS_ASSUME_NONNULL_END
