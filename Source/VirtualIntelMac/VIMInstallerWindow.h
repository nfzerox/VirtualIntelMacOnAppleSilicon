#import <Cocoa/Cocoa.h>
@interface VIMInstallerWindow : NSWindowController
+ (void)showInstaller;
/// Returns YES when shutdown must wait; completion runs on main after work exits.
+ (BOOL)stopForTermination:(void (^)(void))completion;
@end
