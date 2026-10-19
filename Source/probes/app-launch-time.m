#import <AppKit/AppKit.h>
#include <time.h>
#include <libproc.h>
#include <sys/resource.h>
static double now(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}
int main(int argc, char **argv) {
    @autoreleasepool {
        if (argc != 2 && argc != 3) return 64;
        NSURL *url = [NSURL fileURLWithPath:@(argv[1])];
        NSString *identifier = [[NSBundle bundleWithURL:url] bundleIdentifier];
        if (!identifier || [NSRunningApplication runningApplicationsWithBundleIdentifier:identifier].count) {
            fprintf(stderr, "Application missing or already running; refusing misleading timing\n");
            return 65;
        }
        double start = now();
        NSError *error = nil;
        NSMutableDictionary *environment = [NSMutableDictionary dictionary];
        if (argc == 3) environment[@"DYLD_FRAMEWORK_PATH"] = @(argv[2]);
        // Explicit experiment variables only; do not copy SSH credentials or
        // the complete parent environment into the launched application.
        for (NSString *key in @[@"PVG_AIR_CACHE_DIR", @"PVG_TRACE_COMPILER"]) {
            const char *value = getenv(key.UTF8String);
            if (value) environment[key] = @(value);
        }
        NSDictionary *configuration = environment.count ?
            @{NSWorkspaceLaunchConfigurationEnvironment: environment} : @{};
        NSRunningApplication *app = [[NSWorkspace sharedWorkspace]
            launchApplicationAtURL:url options:NSWorkspaceLaunchDefault
            configuration:configuration error:&error];
        if (!app) { fprintf(stderr, "%s\n", error.description.UTF8String); return 1; }
        printf("pid=%d launch-return=%.3fs\n", app.processIdentifier, now()-start);
        fflush(stdout);
        while (!app.finishedLaunching && !app.terminated && now()-start < 45)
            [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
        printf("ready=%d terminated=%d elapsed=%.3fs\n", app.finishedLaunching,
               app.terminated, now()-start);
        fflush(stdout);
        // Separate launch notification from WindowServer ordering a normal
        // window onscreen. This is NOT first-pixel or host-presentation timing.
        // Poll at 10 Hz only after launch, bounded by the same deadline.
        BOOL onscreen = NO;
        unsigned windowQueries = 0;
        double querySeconds = 0, maxQuerySeconds = 0;
        while (!app.terminated && now() - start < 45) {
            double queryStart = now();
            CFArrayRef windows = CGWindowListCopyWindowInfo(
                kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
                kCGNullWindowID);
            double queryDuration = now() - queryStart;
            ++windowQueries;
            querySeconds += queryDuration;
            if (queryDuration > maxQuerySeconds) maxQuerySeconds = queryDuration;
            for (NSDictionary *window in (NSArray *)windows) {
                if ([window[(id)kCGWindowOwnerPID] intValue] == app.processIdentifier &&
                    [window[(id)kCGWindowLayer] intValue] == 0 &&
                    [window[(id)kCGWindowAlpha] doubleValue] > 0) {
                    onscreen = YES;
                    break;
                }
            }
            if (windows) CFRelease(windows);
            if (onscreen) break;
            [[NSRunLoop currentRunLoop] runUntilDate:
                [NSDate dateWithTimeIntervalSinceNow:0.1]];
        }
        printf("window-ordered-onscreen=%d elapsed=%.3fs (not pixels presented)\n",
               onscreen, now()-start);
        printf("window-queries=%u query-total=%.3fs query-max=%.3fs\n",
               windowQueries, querySeconds, maxQuerySeconds);
        // Child's accumulated CPU time through the window observation includes
        // work before launch-return (not directly comparable to ready-only runs).
        // It excludes helper processes and is not a critical-path breakdown.
        struct rusage_info_v2 usage = {0};
        if (proc_pid_rusage(app.processIdentifier, RUSAGE_INFO_V2,
                           (rusage_info_t *)&usage) == 0) {
            printf("app-user=%.3fs app-system=%.3fs pageins=%llu disk-read=%llu\n",
                   usage.ri_user_time / 1e9, usage.ri_system_time / 1e9,
                   (unsigned long long)usage.ri_pageins,
                   (unsigned long long)usage.ri_diskio_bytesread);
        } else {
            perror("proc_pid_rusage");
        }
        return app.finishedLaunching && onscreen ? 0 : 1;
    }
}
