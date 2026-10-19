#import <Foundation/Foundation.h>
@class VIMMetalView;
NS_ASSUME_NONNULL_BEGIN
/// One helper per connection. All public methods are main-thread only.
@interface VIMDisplayConnection : NSObject
@property(nonatomic, readonly) NSString *socketPath;
/// Creates a private listener before QEMU/helper launch. Error callback is main-thread.
- (nullable instancetype)initWithView:(VIMMetalView *)view
    failure:(void (^)(NSError *error))failure error:(NSError **)error;
- (void)stop;
- (void)requestWidth:(NSUInteger)width height:(NSUInteger)height;
- (void)requestDisplaySleeping:(BOOL)sleeping;
@end
NS_ASSUME_NONNULL_END
