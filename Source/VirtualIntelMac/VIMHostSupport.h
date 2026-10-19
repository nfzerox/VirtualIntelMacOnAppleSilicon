#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
/// Nil when the host release is supported; otherwise the required update notice.
NSString *_Nullable VIMHostUpdateNotice(NSOperatingSystemVersion version);
/// UI-only builds may use local resources; packaged apps always use their runtime.
NSString *_Nullable VIMDevelopmentSourceRoot(void);
/// Test actual Intel execution on Apple silicon, not a version-specific receipt
/// or the Linux Rosetta API (which no longer proves Mac Rosetta on macOS 27).
/// The bundled graphics helper exits immediately with no arguments or GPU work.
void VIMCheckRosetta(void (^completion)(BOOL missing,NSError *_Nullable error));
/// Fail-closed start preflight, including a missing/broken graphics helper.
void VIMCheckRosettaForBoot(void (^completion)(BOOL missing,NSError *_Nullable error));
@class NSWindow;
/// Shared launch/start alert. Installing never queues an automatic VM start.
void VIMPresentRosettaRequiredAlert(NSWindow *_Nullable window);
/// An owned, process-scoped idle-sleep assertion. Release to restore normal sleep.
/// Does not prevent display sleep or explicit user-requested system sleep.
@interface VIMInstallationActivity : NSObject
+ (instancetype)beginWithReason:(NSString *)reason;
@end
NS_ASSUME_NONNULL_END
