#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
@interface VIMMachine : NSObject
@property(nonatomic, readonly) NSURL *URL;
@property(nonatomic, readonly) NSDictionary *configuration;
@property(nonatomic, readonly) NSString *name;
@property(nonatomic, readonly) NSString *guest;
/// PassthroughOpenGL family; independent of developer instrumentation flags.
@property(nonatomic, readonly) BOOL preMetalGuest;
@property(nonatomic, readonly) BOOL passthroughOpenGLEnabled;
/// Explicit CLI-only legacy research session; never enables graphics setup.
@property(nonatomic, readonly) BOOL openGLResearchGuest;
@property(nonatomic, readonly) NSDictionary *editableConfiguration;
/// Configured capacity, not sparse-file allocation. Zero means unavailable.
/// Imported QCOW2 capacity is read once per loaded model, without qemu-img.
@property(nonatomic, readonly) uint64_t diskCapacityBytes;
@property(nonatomic, readonly, nullable) NSString *pendingName;
/// Applies queued changes / migrates legacy package names only while stopped.
/// A running VM is a successful deferred operation, never moved in use.
- (BOOL)applyPendingChanges:(NSError **)error;
/// Media ownership and display selection are independent: a completed native
/// installation boots PVG for verification while its ISO is still retained.
@property(nonatomic, readonly) BOOL requiresInstallerDisplay;
/// Includes the last live observation of an in-place update for a new QEMU launch.
@property(nonatomic, readonly) BOOL requiresInstallerDisplayForBoot;
/// A native update boot or an update awaiting verified graphics recovery.
/// Separate from adapter choice: published drivers can boot PVG before the
/// new-build post-reboot verification completes.
@property(nonatomic, readonly) BOOL hasPendingGuestUpdate;
/// High Sierra's separate update OS needs the standard adapter while selected.
@property(nonatomic, readonly) BOOL requiresStandardVGAForUpdate;
/// Published transaction evidence, independent of ISO ownership/import state.
@property(nonatomic, readonly) BOOL hasPublishedGuestGraphics;
/// Narrow first-boot EFI admission for a host-assembled assistant disk.
@property(nonatomic, readonly) BOOL hasHostStagedAssistantGraphics;
@property(nonatomic, readonly) BOOL hasHostStagedPVGVerifier;
@property(nonatomic, readonly) BOOL hasHostStagedInitialPreferences;
/// Host preparation is published, but its first accelerated desktop is unverified.
@property(nonatomic, readonly) BOOL hasPendingHostAssemblyVerification;
/// Host-built systems have no in-guest installation transaction to reconcile.
- (BOOL)hostAssemblyNeedsGraphicsVerificationWithGuestReport:(NSDictionary *)report;
- (BOOL)recordHostAssemblyGraphicsVerification:(NSDictionary *)evidence guestReport:(NSDictionary *)report error:(NSError **)error;
/// Widest display this guest survives, in pixels (0: no limit). The release's
/// limit, lifted once post-boot verification found private window tiles.
@property(nonatomic, readonly) CGFloat guestDisplayMaxWidth;
/// Big Sur's own driver can accelerate Setup Assistant before GL patching.
/// This evidence never marks installation/graphics verification complete.
- (BOOL)prepareNativeBigSurDisplayWithGuestReport:(NSDictionary *)report error:(NSError **)error;
/// Called under the session lock before launch. Preserve updater software video
/// across its reboots until freshly published support for the new OS is ready.
- (BOOL)prepareUpdaterDisplayForBoot:(NSError **)error;
+ (nullable instancetype)loadURL:(NSURL *)URL error:(NSError **)error;
+ (nullable instancetype)importDisk:(NSURL *)disk guest:(NSString *)guest name:(NSString *)name
    library:(NSURL *)library firmware:(NSURL *)firmware nvram:(NSURL *)nvram
    bootMedia:(NSURL *)bootMedia qemuImage:(NSURL *)qemuImage error:(NSError **)error;
/// Creates an independent formatted disk and references shared installer media.
+ (nullable instancetype)createGuest:(NSString *)guest name:(NSString *)name diskGiB:(NSUInteger)diskGiB
    library:(NSURL *)library firmware:(NSURL *)firmware nvram:(NSURL *)nvram
    bootMedia:(NSURL *)bootMedia installer:(NSURL *)installer qemuImage:(NSURL *)qemuImage error:(NSError **)error;
/// Recover/publish only, under the worker ownership lock. Nil without an error
/// means no completed disk yet; no installer, credential or scratch budget needed.
+ (nullable instancetype)recoverHostInstallJob:(NSURL *)job library:(NSURL *)library guest:(NSString *)guest
    name:(NSString *)name cores:(NSUInteger)cores memoryMiB:(NSUInteger)memory error:(NSError **)error;
/// Publish the worker's Prepared.vintelmac inside Machines/.host-install-UUID.
/// The worker must have closed the disk and detached its mounts. A retry after
/// publication returns the original Mac, even if its name has since changed.
+ (nullable instancetype)publishHostInstallJob:(NSURL *)job library:(NSURL *)library guest:(NSString *)guest
    name:(NSString *)name cores:(NSUInteger)cores memoryMiB:(NSUInteger)memory error:(NSError **)error;
- (BOOL)rename:(NSString *)name error:(NSError **)error;
- (BOOL)saveSettings:(NSDictionary *)settings error:(NSError **)error;
/// Pre-first-boot, non-secret policy only; credentials stay private on the host.
- (BOOL)saveAutomaticSetup:(NSDictionary *)profile error:(NSError **)error;
/// Atomically claims the initial automatic patch attempt for an app-created VM.
/// Returns NO without error when ineligible/already attempted; never retries.
- (BOOL)claimAutomaticInstallationWithGuestReport:(NSDictionary *)report error:(NSError **)error;
/// Claims one automatic graphics repair after a minor guest OS build change.
/// Requires verified prior support on the same volume and a live new desktop;
/// preserves the old journal and never retries an ambiguous claimed attempt.
- (BOOL)claimAutomaticUpdateRepairWithGuestReport:(NSDictionary *)report error:(NSError **)error;
/// Same evidence guards without creating/repeating a claim. Used only when the
/// user explicitly requests repair after a known preflight failure.
- (BOOL)canRepairUpdatedSystemWithGuestReport:(NSDictionary *)report error:(NSError **)error;
/// Only while stopped, after persisted postboot verification and matching live
/// guest status. Retires boot routing before deleting owned, unreferenced media.
- (BOOL)finishInstallationWithGuestReport:(NSDictionary *)report error:(NSError **)error;
/// Retries only a previously authorized cleanup, with all ownership and lock
/// checks repeated. No pending cleanup is a successful no-op.
- (BOOL)retryInstallerCleanup:(NSError **)error;
/// Classic setup uses its own volume/GL proof, not a fabricated Metal receipt.
/// Retire installer routing before the orderly first-login restart. Cleanup is
/// deferred until the old QEMU has closed the media and the desktop is verified.
- (BOOL)prepareOpenGLRestartWithReport:(NSDictionary *)report error:(NSError **)error;
- (BOOL)recordOpenGLRestartAcknowledged:(NSError **)error;
- (BOOL)finishOpenGLSetupWithReport:(NSDictionary *)report error:(NSError **)error;
@end
NS_ASSUME_NONNULL_END
