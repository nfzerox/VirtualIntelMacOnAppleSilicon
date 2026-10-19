#import <Foundation/Foundation.h>
@class VIMGuestAgent;
NS_ASSUME_NONNULL_BEGIN
@interface VIMGuestProgress : NSObject
@property(nonatomic,readonly) NSDictionary *receipt;
+ (void)startWithAgent:(VIMGuestAgent *)agent staging:(NSString *)directory
           completion:(void (^)(VIMGuestProgress *_Nullable,NSError *_Nullable))completion;
+ (nullable instancetype)progressWithAgent:(VIMGuestAgent *)agent receipt:(NSDictionary *)receipt;
- (void)updateMode:(NSString *)mode fraction:(double)fraction message:(NSString *)message
       completion:(void (^_Nullable)(NSError *_Nullable))completion;
- (void)waitForRestartChoice:(void (^)(NSString *_Nullable,NSError *_Nullable))completion;
@end
NS_ASSUME_NONNULL_END
