#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
/// A Tiger–Snow Leopard restore disc other than the qualified dot release.
/// userInfo carries VIMSupportedReleaseURLKey: the supported image to download.
extern NSString *const VIMInstallerReleaseErrorDomain;
extern NSString *const VIMSupportedReleaseURLKey;
/// Extracts trusted Apple installer inputs without executing their installer scripts.
/// The caller supplies a new job directory on its capacity-checked scratch volume.
/// Kernel flags this app writes into Big Sur and later installer media. A boot
/// carrying them runs from that media, not from a staged installation.
extern NSString *const VIMInstallerMediaKernelFlags;
@interface VIMInstallerAssembly : NSObject
/// Worker operation: installer apps or read-only Tiger/Leopard/Snow restore DVDs.
+ (nullable NSString *)guestForInstallerApplication:(NSURL *)URL error:(NSError **)error;
+ (BOOL)validateInstallerApplication:(NSURL *)URL guest:(NSString *)guest error:(NSError **)error;
/// Cheap launch-time check of an app-prepared image against its size receipt.
/// Ordinary user-supplied images without a preparation receipt remain supported.
+ (BOOL)validatePreparedMedia:(NSURL *)URL guest:(NSString *)guest error:(NSError **)error;
/// Only app-owned completed Apple transfers can be retired; active downloads
/// and user-selected installer applications are never removed.
+ (void)retireCompletedDownloads:(NSDictionary<NSString *,NSURL *> *)files;
/// Receipt-validated retained outputs, never partially prepared media.
+ (NSDictionary<NSString *,NSURL *> *)retainedInstallerForGuest:(NSString *)guest library:(NSURL *)library;
@property(atomic) BOOL cancelled;
/// Explicit CLI/build harness input; production defaults to bundled support.
@property(nonatomic, nullable) NSURL *openGLSupportDirectory;
/// Development media harness; production uses bundled GuestTools.
@property(nonatomic, nullable) NSURL *recoveryControlTool;
@property(nonatomic, copy, nullable) void (^progress)(NSString *stage);
/// Main-thread progress, using tool percentages or measured/estimated durations.
@property(nonatomic, copy, nullable) void (^workProgress)(double fraction);
@property(nonatomic, copy, nullable) void (^timeRemaining)(NSTimeInterval seconds);
/// Host worker protocol: read Request.plist and private UTF-8 stdin, put the
/// completed bundle in the attempt as Prepared.vintelmac, and honor Cancel.
/// Do not exit until all tools and scratch mounts have been released.
- (BOOL)runHostInstallWorker:(NSURL *)worker attempt:(NSURL *)attempt request:(NSDictionary *)request
    password:(NSString *)password error:(NSError **)error;
/// With the creation-job lock held, recover a completed attempt stranded by
/// an app interruption. A still-running worker must finish before any retry.
+ (BOOL)recoverHostInstallInDirectory:(NSURL *)directory error:(NSError **)error;
/// With the same creation lock held, detach an abandoned attempt's own images
/// and retire regenerable scratch files before allocating its replacement.
+ (BOOL)cleanHostInstallAttemptsInDirectory:(NSURL *)directory error:(NSError **)error;
/// Blocking worker operation; never call on the main thread. Originals are read-only.
/// Returns an assembled installer app, not a bootable ISO.
- (nullable NSURL *)assembleGuest:(NSString *)guest
                           files:(NSDictionary<NSString *, NSURL *> *)files
                    jobDirectory:(NSURL *)directory error:(NSError **)error;
/// Builds a Big Sur HFS/UDF installer image from an assembled installer.
/// Uses a separate fresh external job; does not erase any volume or need root.
/// Big Sur and later: boot media from an assembled InstallAssistant app.
- (nullable NSURL *)createAssistantMediaFromInstaller:(NSURL *)installer guest:(NSString *)guest
                                          jobDirectory:(NSURL *)directory error:(NSError **)error;
- (nullable NSURL *)createBigSurMediaFromInstaller:(NSURL *)installer
                                     jobDirectory:(NSURL *)directory error:(NSError **)error;
- (nullable NSURL *)createRecoveryMediaFromInstaller:(NSURL *)installer guest:(NSString *)guest
                                    jobDirectory:(NSURL *)directory error:(NSError **)error;
@end
NS_ASSUME_NONNULL_END
