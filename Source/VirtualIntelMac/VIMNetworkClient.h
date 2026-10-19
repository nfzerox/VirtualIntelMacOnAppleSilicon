#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
/// Owns one authorized native vmnet capability. The VM process stays ordinary.
@interface VIMNetworkClient : NSObject
@property(nonatomic,readonly,nullable) NSFileHandle *socket;
@property(nonatomic,readonly) NSDictionary *details;
- (void)startMode:(NSString *)mode bridge:(NSString *)bridge completion:(void (^)(NSError *_Nullable))completion;
- (void)stop;
+ (void)authorizeUsingTerminalWithCompletion:(void (^)(NSError *_Nullable))completion;
@end
NS_ASSUME_NONNULL_END
