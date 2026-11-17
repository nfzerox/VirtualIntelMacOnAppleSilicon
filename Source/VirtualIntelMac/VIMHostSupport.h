#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
/// PVG-backed guests have additional host requirements.
BOOL VIMGuestRequiresPVGHostSupport(NSString *guest);
/// Period startup/APFS tools still run as Intel binaries on these native-install routes.
BOOL VIMHostInstallerRequiresRosetta(NSString *guest,BOOL userSelectedInstaller);
NSString *_Nullable VIMHostUpdateNoticeForGuest(NSOperatingSystemVersion version,NSString *guest);
/// UI-only builds may use local resources; packaged apps always use their runtime.
NSString *_Nullable VIMDevelopmentSourceRoot(void);
/// Test actual Intel execution on Apple silicon, not a version-specific receipt
/// or the Linux Rosetta API (which no longer proves Mac Rosetta on macOS 27).
/// The bundled graphics helper exits immediately with no arguments or GPU work.
/// Fail closed if the graphics helper is missing or broken.
void VIMCheckRosettaForPVG(void (^completion)(BOOL missing,NSError *_Nullable error));
void VIMCheckRosettaForHostInstaller(void (^completion)(BOOL missing,NSError *_Nullable error));
@class NSWindow;
/// Installing never queues an automatic continuation.
void VIMPresentRosettaRequiredAlert(NSWindow *_Nullable window,BOOL creating);
void VIMPresentHostInstallerRosettaAlert(NSWindow *_Nullable window);
void VIMPresentHostUpdateRequiredAlert(NSString *notice,NSWindow *_Nullable window);
/// An owned, process-scoped idle-sleep assertion. Release to restore normal sleep.
/// Does not prevent display sleep or explicit user-requested system sleep.
@interface VIMInstallationActivity : NSObject
+ (instancetype)beginWithReason:(NSString *)reason;
@end
NS_ASSUME_NONNULL_END
