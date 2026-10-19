#import <Cocoa/Cocoa.h>
NS_ASSUME_NONNULL_BEGIN
/// Prefer the supplied window, then an active app window. AppKit queues sheets;
/// a standalone alert is used only when there is no window to attach to.
void VIMPresentAlert(NSAlert *alert, NSWindow *_Nullable window,
                     void (^_Nullable completion)(NSModalResponse response));
void VIMPresentError(NSError *_Nullable error, NSWindow *_Nullable window);
/// Marks an install-flow alert; all other alerts explicitly use the app icon.
void VIMSetInstallerAlertIcon(NSAlert *alert, NSString *guest);
void VIMSetAppAlertIcon(NSAlert *alert);
NS_ASSUME_NONNULL_END
