#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN
@interface VIMUpdateChecker : NSObject
@property(nonatomic, readonly, getter=isChecking) BOOL checking;
- (void)checkManually:(BOOL)manual;
+ (nullable NSString *)stableVersion:(id)value;
+ (NSComparisonResult)compareVersion:(NSString *)left toVersion:(NSString *)right;
@end
NS_ASSUME_NONNULL_END
