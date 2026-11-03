#import <Foundation/Foundation.h>
@class VIMMetalView;
NS_ASSUME_NONNULL_BEGIN
/// One helper per connection. All public methods are main-thread only.
@interface VIMDisplayConnection : NSObject
@property(nonatomic, readonly) NSString *socketPath;
/// Optional host renderer capability endpoint; absent on the normal CPU path.
- (nullable NSString *)enableSurfaceTransportForRendererPID:(pid_t)pid error:(NSError **)error;
- (void)disableSurfaceTransport;
/// Creates a private listener before QEMU/helper launch. Error callback is main-thread.
- (nullable instancetype)initWithView:(VIMMetalView *)view
    failure:(void (^)(NSError *error))failure error:(NSError **)error;
- (void)stop;
- (void)requestWidth:(NSUInteger)width height:(NSUInteger)height;
- (void)requestDisplaySleeping:(BOOL)sleeping;
@end
NS_ASSUME_NONNULL_END
