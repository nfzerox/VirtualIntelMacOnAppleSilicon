// A deterministic local banner, independent of Software Update/catalog data.
// Bundle this as an LSUIElement app (10.8+) and open as the desktop user.
#import <AppKit/AppKit.h>

@interface NotificationProbe : NSObject <NSApplicationDelegate,NSUserNotificationCenterDelegate>
@end

@implementation NotificationProbe
- (void)applicationDidFinishLaunching:(NSNotification *)note {
    (void)note;
    NSUserNotificationCenter *center=[NSUserNotificationCenter defaultUserNotificationCenter];
    center.delegate=self;
    NSUserNotification *banner=[NSUserNotification new];
    banner.title=@"Virtual Intel Mac graphics check";
    banner.subtitle=@"Notification subtitle";
    banner.informativeText=@"This is known local notification text.";
    banner.hasActionButton=NO;
    [center deliverNotification:banner];
    [banner release];
}
- (BOOL)userNotificationCenter:(NSUserNotificationCenter *)center shouldPresentNotification:(NSUserNotification *)notification {
    (void)center;(void)notification;return YES;
}
- (void)userNotificationCenter:(NSUserNotificationCenter *)center didDeliverNotification:(NSUserNotification *)notification {
    (void)center;(void)notification;[NSApp terminate:nil];
}
@end

int main(void) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        NotificationProbe *probe=[NotificationProbe new];
        [NSApp setDelegate:probe];
        [NSApp run];
        [probe release];
    }
    return 0;
}
