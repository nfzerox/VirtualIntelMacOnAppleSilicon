#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
@class VIMMachine;
/// Non-secret setup policy and private temporary host-credential lifetime. Passwords
/// never belong in Machine.plist, user defaults, process arguments or receipts.
@interface VIMAutomaticSetup : NSObject
+ (BOOL)supportsGuest:(NSString *)guest;
+ (NSString *)defaultUsername;
+ (BOOL)validateUsername:(NSString *)username error:(NSError **)error;
+ (BOOL)validatePassword:(NSString *)password error:(NSError **)error;
+ (BOOL)savePassword:(NSString *)password identifier:(NSString *)identifier error:(NSError **)error;
+ (nullable NSString *)passwordForIdentifier:(NSString *)identifier error:(NSError **)error;
+ (BOOL)removePasswordForIdentifier:(NSString *)identifier error:(NSError **)error;
/// Apple's native OSInstaller automation contract. UUID selection, never an
/// ambiguous translated/name-only target and never an implicit erase request.
+ (nullable NSDictionary *)installerConfigurationForVolumeUUID:(NSString *)UUID error:(NSError **)error;
/// Tiny read-only per-machine policy disc; contains NO password. A missing
/// target journal on an imported/research machine returns nil without error.
+ (nullable NSURL *)configurationMediaForMachine:(VIMMachine *)machine error:(NSError **)error;
@end
NS_ASSUME_NONNULL_END
