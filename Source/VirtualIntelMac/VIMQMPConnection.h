#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
/// Serialized QEMU control channel. Completions always run on the main thread.
@interface VIMQMPConnection : NSObject
/// Lifecycle events observed on the existing connection, delivered on main.
/// Set before connecting. No second socket or additional polling is used.
@property(nonatomic,copy,nullable) void (^lifecycleEventHandler)(NSDictionary *event);
- (instancetype)initWithSocketPath:(NSString *)path;
/// QGA has no greeting and Apple's implementation may omit response IDs.
- (instancetype)initWithGuestAgentSocketPath:(NSString *)path;
- (void)connectWithCompletion:(void (^)(NSError *_Nullable error))completion;
- (void)execute:(NSString *)command arguments:(NSDictionary *)arguments
    completion:(void (^)(id _Nullable result, NSError *_Nullable error))completion;
/// After the owned emulator has exited, consume already queued lifecycle events
/// before teardown. No commands, reconnect, or second control client.
- (void)drainLifecycleEventsAfterExitWithCompletion:(void (^)(void))completion;
- (void)close;
@end
NS_ASSUME_NONNULL_END
