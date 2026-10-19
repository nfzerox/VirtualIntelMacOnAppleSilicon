// Bounded, read-only timing probe. No injection, application launch, or cache
// warming: establish when public workspace notifications actually arrive.
#import <AppKit/AppKit.h>
#include <time.h>
#include <unistd.h>

static double Now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

int main(int argc, char **argv) {
    @autoreleasepool {
        if (argc != 2 || getuid() == 0) {
            fprintf(stderr, "Run as the console user: workspace-launch-observer BUNDLE_ID\n");
            return 64;
        }
        NSString *identifier = @(argv[1]);
        NSNotificationCenter *center = NSWorkspace.sharedWorkspace.notificationCenter;
        NSMutableArray *tokens = [NSMutableArray array];
        for (NSString *name in @[NSWorkspaceWillLaunchApplicationNotification,
                                 NSWorkspaceDidLaunchApplicationNotification,
                                 NSWorkspaceDidTerminateApplicationNotification]) {
            id token = [center addObserverForName:name object:nil queue:nil
                usingBlock:^(NSNotification *notification) {
                NSRunningApplication *app = notification.userInfo[NSWorkspaceApplicationKey];
                if (![app.bundleIdentifier isEqual:identifier]) return;
                NSDictionary *record = @{@"Event":name, @"Seconds":@(Now()),
                    @"PID":@(app.processIdentifier), @"FinishedLaunching":@(app.finishedLaunching)};
                NSData *json = [NSJSONSerialization dataWithJSONObject:record options:0 error:nil];
                printf("%s\n", [[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding].UTF8String);
                fflush(stdout);
            }];
            [tokens addObject:token];
        }
        printf("{\"Ready\":true,\"Seconds\":%.6f}\n", Now());
        fflush(stdout);
        NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:60];
        while (deadline.timeIntervalSinceNow > 0)
            [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
        for (id token in tokens) [center removeObserver:token];
        return 0;
    }
}
