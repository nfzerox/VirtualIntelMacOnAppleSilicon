#import <AppKit/AppKit.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: set-desktop-picture /absolute/image/path\n");
        return 64;
    }
    @autoreleasepool {
        NSString *path = [NSString stringWithUTF8String:argv[1]];
        if (![path isAbsolutePath] ||
            ![[NSFileManager defaultManager] fileExistsAtPath:path]) {
            fprintf(stderr, "desktop image must be an existing absolute path\n");
            return 66;
        }
        NSURL *image = [NSURL fileURLWithPath:path];
        NSArray<NSScreen *> *screens = [NSScreen screens];
        if (screens.count == 0) {
            fprintf(stderr, "no GUI screen available\n");
            return 69;
        }
        NSWorkspace *workspace = [NSWorkspace sharedWorkspace];
        for (NSScreen *screen in screens) {
            NSURL *old = [workspace desktopImageURLForScreen:screen];
            fprintf(stderr, "prior desktop: %s\n",
                    old ? old.fileSystemRepresentation : "(none)");
            NSError *error = nil;
            BOOL ok = [workspace setDesktopImageURL:image forScreen:screen
                                           options:@{}
                                             error:&error];
            if (!ok) {
                fprintf(stderr, "set desktop failed: %s\n",
                        error.localizedDescription.UTF8String);
                return 1;
            }
            NSURL *current = [workspace desktopImageURLForScreen:screen];
            fprintf(stderr, "current desktop: %s\n",
                    current ? current.fileSystemRepresentation : "(none)");
        }
    }
    return 0;
}
