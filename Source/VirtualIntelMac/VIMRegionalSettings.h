#import <Foundation/Foundation.h>
@class VIMGuestAgent;
NS_ASSUME_NONNULL_BEGIN

/// Non-secret, immutable first-install intent. Independent of app translations.
@interface VIMRegionalSettings : NSObject
+ (nullable NSDictionary *)hostSnapshotWithError:(NSError **)error;
+ (nullable NSDictionary *)normalizeRequest:(id)request error:(NSError **)error;
+ (NSArray<NSDictionary<NSString *,NSString *> *> *)languageChoices;
+ (NSArray<NSDictionary<NSString *,NSString *> *> *)regionChoices;
+ (NSArray<NSDictionary<NSString *,NSString *> *> *)timeZoneChoices;
+ (void)preflightRequest:(nullable NSDictionary *)request agent:(VIMGuestAgent *)agent executable:(NSString *)executable
              packageURL:(NSURL *)package completion:(void (^)(NSError *_Nullable error))completion;
+ (BOOL)storeResult:(id)result requested:(NSDictionary *)request packageURL:(NSURL *)package error:(NSError **)error;
@end

NS_ASSUME_NONNULL_END
