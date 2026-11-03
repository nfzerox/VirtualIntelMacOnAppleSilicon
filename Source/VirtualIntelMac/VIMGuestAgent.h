#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
/// Main-thread owned controller for the local QGA-compatible guest agent. All replies
/// are delivered on the main thread; no command is dispatched through a shell.
@interface VIMGuestAgent : NSObject
/// Modern VirtIO defaults to 0.25 s. The legacy UART uses a slower cold-work
/// cadence; this never creates an idle/background polling timer.
@property(nonatomic) NSTimeInterval processPollInterval;
@property(nonatomic) BOOL preMetalGuest;
@property(nonatomic) BOOL tigerGuest;
/// Mojave/Catalina Recovery lacks Apple's agent. Only connection handshakes
/// may fall back to this private UART; commands are never replayed on error.
@property(nonatomic, copy, nullable) NSString *recoverySocketPath;
/// Selected endpoint after a successful handshake, for diagnostic records.
/// Recovery's UART is not Apple's installed-system VirtIO agent.
@property(nonatomic, readonly) NSString *transportName;
/// Execute in the actual console user's bootstrap session, using the period
/// launchctl bsexec API on later pre-Metal guests, or Tiger's native child
/// bootstrap-port launcher, rather than unsupported asuser.
- (void)runConsoleExecutable:(NSString *)path arguments:(NSArray<NSString *> *)arguments
                    timeout:(NSTimeInterval)timeout completion:(void (^)(NSDictionary *_Nullable,NSError *_Nullable))completion;
- (instancetype)initWithSocketPath:(NSString *)path;
- (void)connectWithCompletion:(void (^)(NSError *_Nullable error))completion;
/// Result contains the QGA exit status plus decoded stdout/stderr NSData.
/// A timeout has an uncertain outcome: callers must not blindly repeat a write.
/// Pass zero for setup work that must wait for actual process completion.
/// Closing the agent or a transport error still interrupts the host's wait.
- (void)runExecutable:(NSString *)path arguments:(NSArray<NSString *> *)arguments
              timeout:(NSTimeInterval)timeout
           completion:(void (^)(NSDictionary *_Nullable result, NSError *_Nullable error))completion;
/// Small private setup input goes through stdin, never argv or a saved command
/// file. Intended for account provisioning; bulk payloads belong on support media.
- (void)runExecutable:(NSString *)path arguments:(NSArray<NSString *> *)arguments
        standardInput:(nullable NSData *)input timeout:(NSTimeInterval)timeout
           completion:(void (^)(NSDictionary *_Nullable result, NSError *_Nullable error))completion;
/// Stages the read-only status probe in a root-only private run directory.
/// Reuse requires unchanged requested bytes and root-only cache metadata;
/// every call executes it again for fresh evidence. Never marks a VM installed.
- (void)collectStatusWithTool:(NSData *)tool
                  completion:(void (^)(NSDictionary *_Nullable report, NSError *_Nullable error))completion;
- (void)collectInstalledOpenGLDesktopStatus:(void (^)(NSDictionary *_Nullable,NSError *_Nullable))completion;
/// Open an allowed System Preferences pane in the signed-in guest's GUI session.
/// Does not enable services, change permissions, collect passwords or use AppleEvents.
- (void)openPreferencesPane:(NSString *)pane completion:(void (^)(NSError *_Nullable error))completion;
/// Open an SMB/SSH URL after the session validates the actual host network IP.
- (void)openHostConnection:(NSURL *)URL completion:(void (^)(NSError *_Nullable error))completion;
/// Start macOS shutdown, acknowledging the spawned command rather than polling
/// its exit after the agent has gone away. Never retry an uncertain request.
- (void)requestShutdownWithCompletion:(void (^)(NSError *_Nullable error))completion;
- (void)requestRestartWithCompletion:(void (^)(NSError *_Nullable error))completion;
/// Upload into a new root-owned temporary directory with acknowledged writes.
/// Transfers only the requested files through the guest agent. Never installs
/// files or retries ambiguous writes. Failure retains staging for diagnosis.
- (void)stageFiles:(NSDictionary<NSString *,NSURL *> *)files
          progress:(void (^_Nullable)(uint64_t completed,uint64_t total))progress
        completion:(void (^)(NSDictionary *_Nullable receipt,NSError *_Nullable error))completion;
/// Installation journals, backups and verification tools must survive macOS
/// updates, which may remove /var/tmp. Uses root-only /private/var/db staging.
- (void)stageInstallationFiles:(NSDictionary<NSString *,NSURL *> *)files
          progress:(void (^_Nullable)(uint64_t completed,uint64_t total))progress
        completion:(void (^)(NSDictionary *_Nullable receipt,NSError *_Nullable error))completion;
- (void)close;
@end
NS_ASSUME_NONNULL_END
