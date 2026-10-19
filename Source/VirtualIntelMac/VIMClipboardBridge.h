#import <AppKit/AppKit.h>
NS_ASSUME_NONNULL_BEGIN
/// Per-VM private virtual-serial text channel. Owner must provision token only to that VM's
/// non-root GUI helper and supply foreground/consent policy before use.
@interface VIMClipboardBridge : NSObject
@property(nonatomic, readonly) NSString *token;
@property(nonatomic, readonly, getter=isConnected) BOOL connected;
@property(nonatomic, copy, nullable) BOOL (^exchangeAllowed)(void);
/// Unexpected transport/helper loss only; explicit stop does not invoke this.
@property(nonatomic, copy, nullable) void (^connectionLost)(void);
/// Separate, metadata-only channel. Never accesses either pasteboard.
@property(nonatomic) BOOL displayPowerOnly;
@property(nonatomic, copy, nullable) void (^displayPowerChanged)(BOOL sleeping);
- (BOOL)startWithSocketPath:(NSString *)path error:(NSError **)error;
- (void)awaitGuestHandshake;
- (NSDictionary<NSString *,NSNumber *> *)diagnosticState; // counters only, never text/token
- (void)stop;
@end
NS_ASSUME_NONNULL_END
