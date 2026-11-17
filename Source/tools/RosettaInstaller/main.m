// Opening this Intel-only app lets macOS offer its normal Rosetta installer.
// It never installs software, accepts a license, asks for credentials, or
// starts a VM itself. Once translation works, register and immediately quit.
#import <AppKit/AppKit.h>

@interface VIMRosettaInstallerDelegate : NSObject <NSApplicationDelegate>
@end
@implementation VIMRosettaInstallerDelegate
- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    (void)notification;
    dispatch_async(dispatch_get_main_queue(), ^{ [NSApp terminate:nil]; });
}
@end

int main(void) {
    @autoreleasepool {
        NSApplication *application=NSApplication.sharedApplication;
        __attribute__((objc_precise_lifetime)) VIMRosettaInstallerDelegate *delegate=[VIMRosettaInstallerDelegate new];
        application.delegate=delegate;
        [application run];
    }
    return 0;
}
