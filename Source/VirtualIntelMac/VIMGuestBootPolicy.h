#import <Foundation/Foundation.h>
@class VIMGuestAgent;
NS_ASSUME_NONNULL_BEGIN
/// Preference-first migration/recovery. No TCC mutation, injection or graphics
/// reinstallation. Unknown writes are inspected, never redispatched. The caller
/// may normally restart a conclusively repaired pre-desktop boot.
@interface VIMGuestBootPolicy : NSObject
+ (BOOL)readyAtURL:(NSURL *)journal guest:(NSString *)guest identifier:(NSString *)identifier;
+ (nullable NSString *)verifierAtURL:(NSURL *)journal guest:(NSString *)guest identifier:(NSString *)identifier volume:(NSString *)volume;
+ (void)prepareWithAgent:(VIMGuestAgent *)agent report:(NSDictionary *)report
             identifier:(NSString *)identifier journal:(NSURL *)journal tools:(NSURL *)tools
             completion:(void (^)(BOOL ready,NSError *_Nullable error))completion;
@end
NS_ASSUME_NONNULL_END
