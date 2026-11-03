#import <Foundation/Foundation.h>
@class VIMMachine,VIMGuestAgent;
NS_ASSUME_NONNULL_BEGIN
/// Cold, first-install orchestration over the existing app-owned guest channel.
/// Account passwords travel through private standard input, never saved media.
@interface VIMModernSetup : NSObject
@property(nonatomic,copy,nullable) void (^progress)(NSString *message);
@property(nonatomic,copy,nullable) void (^showAccountProgress)(BOOL automaticLogin);
- (instancetype)initWithMachine:(VIMMachine *)machine agent:(VIMGuestAgent *)agent;
- (void)advanceReport:(NSDictionary *)report completion:(void (^)(NSDictionary *_Nullable,NSError *_Nullable))completion;
@end
NS_ASSUME_NONNULL_END
