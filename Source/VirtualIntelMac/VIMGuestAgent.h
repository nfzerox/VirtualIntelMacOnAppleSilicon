#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
/// Main-thread owned controller for Apple's built-in guest agent. All replies
/// are delivered on the main thread; no command is dispatched through a shell.
@interface VIMGuestAgent : NSObject
- (instancetype)initWithSocketPath:(NSString *)path;
- (void)connectWithCompletion:(void (^)(NSError *_Nullable error))completion;
/// Result contains the QGA exit status plus decoded stdout/stderr NSData.
/// A timeout has an uncertain outcome: callers must not blindly repeat a write.
- (void)runExecutable:(NSString *)path arguments:(NSArray<NSString *> *)arguments
              timeout:(NSTimeInterval)timeout
           completion:(void (^)(NSDictionary *_Nullable result, NSError *_Nullable error))completion;
/// Stages the read-only status probe in a root-only private run directory.
/// Reuse requires unchanged requested bytes and root-only cache metadata;
/// every call executes it again for fresh evidence. Never marks a VM installed.
- (void)collectStatusWithTool:(NSData *)tool
                  completion:(void (^)(NSDictionary *_Nullable report, NSError *_Nullable error))completion;
/// Open an allowed System Preferences pane in the signed-in guest's GUI session.
/// Does not enable services, change permissions, collect passwords or use AppleEvents.
- (void)openPreferencesPane:(NSString *)pane completion:(void (^)(NSError *_Nullable error))completion;
/// Open an SMB/SSH URL after the session validates the actual host network IP.
- (void)openHostConnection:(NSURL *)URL completion:(void (^)(NSError *_Nullable error))completion;
/// Start macOS shutdown, acknowledging the spawned command rather than polling
/// its exit after the agent has gone away. Never retry an uncertain request.
- (void)requestShutdownWithCompletion:(void (^)(NSError *_Nullable error))completion;
/// Upload into a new root-owned temporary directory and verify SHA-256 in guest.
/// Include the bundled guest-macho tool under that exact name. Never installs
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
