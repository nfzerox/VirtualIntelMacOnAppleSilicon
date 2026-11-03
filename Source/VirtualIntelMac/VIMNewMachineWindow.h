#import <Cocoa/Cocoa.h>
@interface VIMNewMachineWindow : NSWindowController
+ (BOOL)isCreating;
+ (BOOL)stopForTermination:(void (^)(void))completion;
+ (void)restorePendingSetup;
+ (void)showWithInstaller:(NSURL *)installer guest:(NSString *)guest;
/// Visible production transfer flow, without creating/booting a guest. Useful
/// for maintaining installer assets and resuming acquisition from the CLI.
+ (void)downloadInstallerForGuest:(NSString *)guest;
+ (void)showConfigurationForGuest:(NSString *)guest;
@end
