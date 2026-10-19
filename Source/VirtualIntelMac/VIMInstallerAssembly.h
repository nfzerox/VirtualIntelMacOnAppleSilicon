#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
/// Extracts trusted Apple installer inputs without executing their installer scripts.
/// The caller supplies a new job directory on its capacity-checked scratch volume.
@interface VIMInstallerAssembly : NSObject
+ (nullable NSString *)guestForInstallerApplication:(NSURL *)URL error:(NSError **)error;
+ (BOOL)validateInstallerApplication:(NSURL *)URL guest:(NSString *)guest error:(NSError **)error;
/// Only app-owned completed Apple transfers can be retired; active downloads
/// and user-selected installer applications are never removed.
+ (void)retireCompletedDownloads:(NSDictionary<NSString *,NSURL *> *)files;
/// Receipt-validated retained outputs, never partially prepared media.
+ (NSDictionary<NSString *,NSURL *> *)retainedInstallerForGuest:(NSString *)guest library:(NSURL *)library;
@property(atomic) BOOL cancelled;
@property(nonatomic, copy, nullable) void (^progress)(NSString *stage);
/// Main-thread progress, using tool percentages or measured/estimated durations.
@property(nonatomic, copy, nullable) void (^workProgress)(double fraction);
@property(nonatomic, copy, nullable) void (^timeRemaining)(NSTimeInterval seconds);
/// Blocking worker operation; never call on the main thread. Originals are read-only.
/// Returns an assembled installer app, not a bootable ISO.
- (nullable NSURL *)assembleGuest:(NSString *)guest
                           files:(NSDictionary<NSString *, NSURL *> *)files
                    jobDirectory:(NSURL *)directory error:(NSError **)error;
/// Builds a Big Sur HFS/UDF installer image from an assembled installer.
/// Uses a separate fresh external job; does not erase any volume or need root.
- (nullable NSURL *)createBigSurMediaFromInstaller:(NSURL *)installer
                                     jobDirectory:(NSURL *)directory error:(NSError **)error;
- (nullable NSURL *)createLegacyMediaFromInstaller:(NSURL *)installer guest:(NSString *)guest
                                    jobDirectory:(NSURL *)directory error:(NSError **)error;
@end
NS_ASSUME_NONNULL_END
