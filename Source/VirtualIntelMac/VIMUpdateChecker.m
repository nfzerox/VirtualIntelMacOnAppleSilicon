#import "VIMUpdateChecker.h"
#import "VIMAlerts.h"

static NSString * const VIMReleasePage = @"https://github.com/nfzerox/VirtualIntelMacOnAppleSilicon/releases";
@interface VIMUpdateChecker ()
@property(nonatomic, readwrite, getter=isChecking) BOOL checking;
@property(nonatomic) BOOL manualRequested;
@end

@implementation VIMUpdateChecker
+ (NSString *)stableVersion:(id)value {
    if (![value isKindOfClass:NSString.class] || [value length] > 48) return nil;
    NSRegularExpression *regex = [NSRegularExpression regularExpressionWithPattern:
        @"^v?([0-9]{1,9})(\\.[0-9]{1,9}){0,3}$" options:0 error:nil];
    NSRange range = NSMakeRange(0, [value length]);
    NSTextCheckingResult *match = [regex firstMatchInString:value options:0 range:range];
    if (!match || !NSEqualRanges(match.range, range)) return nil;
    return [value hasPrefix:@"v"] ? [value substringFromIndex:1] : value;
}
+ (NSComparisonResult)compareVersion:(NSString *)left toVersion:(NSString *)right {
    NSArray *a = [left componentsSeparatedByString:@"."];
    NSArray *b = [right componentsSeparatedByString:@"."];
    for (NSUInteger i = 0; i < MAX(a.count, b.count); i++) {
        long long x = i < a.count ? [a[i] longLongValue] : 0;
        long long y = i < b.count ? [b[i] longLongValue] : 0;
        if (x != y) return x > y ? NSOrderedDescending : NSOrderedAscending;
    }
    return NSOrderedSame;
}
- (void)showTitle:(NSString *)title detail:(NSString *)detail release:(BOOL)release {
    NSAlert *alert = [NSAlert new]; alert.messageText = title; alert.informativeText = detail;
    if (release) [alert addButtonWithTitle:@"View Release"]; [alert addButtonWithTitle:@"OK"];
    VIMPresentAlert(alert,nil,^(NSModalResponse response){
        if(response==NSAlertFirstButtonReturn&&release)[NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:VIMReleasePage]];
    });
}
- (void)checkManually:(BOOL)manual {
    NSAssert(NSThread.isMainThread, @"Update state belongs to the main thread");
    NSUserDefaults *defaults = NSUserDefaults.standardUserDefaults;
    if (self.checking) { self.manualRequested |= manual; return; }
    if (!manual) {
        if (![defaults boolForKey:@"VIMAutomaticUpdateChecks"]) return;
        NSDate *last = [defaults objectForKey:@"VIMLastUpdateAttempt"];
        if ([last isKindOfClass:NSDate.class] && -last.timeIntervalSinceNow < 86400) return;
    }
    self.checking = YES; self.manualRequested = manual;
    [self publishResult:@"Checking for updates…"];
    [defaults setObject:NSDate.date forKey:@"VIMLastUpdateAttempt"];
    NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:
        @"https://api.github.com/repos/nfzerox/VirtualIntelMacOnAppleSilicon/releases/latest"]
        cachePolicy:NSURLRequestReloadIgnoringLocalCacheData timeoutInterval:20];
    [request setValue:@"application/vnd.github+json" forHTTPHeaderField:@"Accept"];
    [request setValue:@"2026-03-10" forHTTPHeaderField:@"X-GitHub-Api-Version"];
    [request setValue:@"VirtualIntelMac-UpdateChecker" forHTTPHeaderField:@"User-Agent"];
    [[[NSURLSession sharedSession] dataTaskWithRequest:request completionHandler:
        ^(NSData *data, NSURLResponse *response, NSError *error) {
        NSInteger status = [response isKindOfClass:NSHTTPURLResponse.class] ?
            ((NSHTTPURLResponse *)response).statusCode : 0;
        id object = data.length && data.length <= 1024 * 1024 ?
            [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
        NSDictionary *release = [object isKindOfClass:NSDictionary.class] ? object : nil;
        NSString *version = [VIMUpdateChecker stableVersion:release[@"tag_name"]];
        BOOL stable = [release[@"draft"] isKindOfClass:NSNumber.class] &&
            [release[@"prerelease"] isKindOfClass:NSNumber.class] &&
            ![release[@"draft"] boolValue] && ![release[@"prerelease"] boolValue];
        dispatch_async(dispatch_get_main_queue(), ^{
            BOOL interactive = self.manualRequested;
            self.checking = NO; self.manualRequested = NO;
            NSString *failure = nil;
            if (error) failure = error.localizedDescription;
            else if (status == 404) failure = @"GitHub has no accessible published release for this project yet.";
            else if (status == 403 || status == 429) failure = @"GitHub temporarily limited update checks. Please try again later.";
            else if (status != 200) failure = [NSString stringWithFormat:@"GitHub returned HTTP %ld. Please try again later.", (long)status];
            else if (!version || !stable) failure = @"The latest release has an unsupported version format. You can check the project’s Releases page.";
            NSString *current = [VIMUpdateChecker stableVersion:
                [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"]];
            if (!failure && !current) failure = @"This development build has no comparable release version.";
            if (failure) {
                [self publishResult:failure];
                if (interactive) [self showTitle:@"Unable to Check for Updates" detail:failure release:YES];
                return;
            }
            if ([VIMUpdateChecker compareVersion:version toVersion:current] == NSOrderedDescending) {
                [self publishResult:[NSString stringWithFormat:@"Version %@ is available. Choose Check Now to view the release.",version]];
                if(!interactive)return;
                if (!interactive && [[defaults stringForKey:@"VIMAnnouncedRelease"] isEqual:version]) return;
                [defaults setObject:version forKey:@"VIMAnnouncedRelease"];
                [self showTitle:@"A New Version Is Available" detail:[NSString stringWithFormat:
                    @"Virtual Intel Mac %@ is available. You’re using %@.", version, current] release:YES];
            } else {
                [self publishResult:[NSString stringWithFormat:@"You’re up to date (%@).",current]];
                if(interactive)[self showTitle:@"You’re Up to Date" detail:[NSString stringWithFormat:
                    @"You’re using Virtual Intel Mac %@. No newer stable release is available.", current] release:NO];
            }
        });
    }] resume];
}
- (void)publishResult:(NSString *)result {
    [NSUserDefaults.standardUserDefaults setObject:result forKey:@"VIMLastUpdateResult"];
    [NSNotificationCenter.defaultCenter postNotificationName:@"VIMUpdateResultChanged" object:self];
}
@end
