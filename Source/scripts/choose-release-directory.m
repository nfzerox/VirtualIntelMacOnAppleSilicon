#import <AppKit/AppKit.h>
int main(void) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        NSOpenPanel *panel=[NSOpenPanel openPanel];
        panel.canChooseDirectories=YES; panel.canChooseFiles=NO;
        panel.canCreateDirectories=YES; panel.allowsMultipleSelection=NO;
        panel.title=@"Choose a Release Build Folder";
        panel.message=@"Choose where to save the complete app and release ZIP. Build files stay on the internal temporary volume.";
        panel.prompt=@"Choose";
        [NSApp activateIgnoringOtherApps:YES];
        if ([panel runModal]!=NSModalResponseOK) return 1;
        puts(panel.URL.fileSystemRepresentation);
    }
    return 0;
}
