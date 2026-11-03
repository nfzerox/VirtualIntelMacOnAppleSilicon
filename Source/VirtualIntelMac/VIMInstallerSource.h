#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
/// Compiled host/protocol policy; asset locations and verification values live
/// in the remotely configurable catalog, not duplicated in executable code.
@interface VIMInstallerSource : NSObject
+ (BOOL)isApprovedURL:(NSURL *)URL;
+ (BOOL)isMavericksRecoveryAsset:(NSURL *)URL;
+ (BOOL)isSnowLeopardRestoreAsset:(NSURL *)URL;
+ (BOOL)isRestoreDVDAsset:(NSURL *)URL forGuest:(NSString *)guest;
/// Error-path guidance only; never blocks a download or reclassifies disk errors.
+ (BOOL)shouldExplainArchiveFailure:(NSError *)error source:(NSURL *)URL guest:(NSString *)guest
    region:(NSString *)region timeZone:(NSString *)timeZone;
/// Worker-only. Session cookies/tokens are ephemeral, never written to a receipt.
+ (nullable NSDictionary<NSString *,NSString *> *)headersForURL:(NSURL *)URL error:(NSError **)error;
/// Validates the pinned recovery payload. Other assets use their recipe's
/// signature/version validation. Reads in bounded chunks, not a whole-file NSData.
+ (BOOL)verifyFile:(NSURL *)file source:(NSURL *)URL error:(NSError **)error;
@end
NS_ASSUME_NONNULL_END
