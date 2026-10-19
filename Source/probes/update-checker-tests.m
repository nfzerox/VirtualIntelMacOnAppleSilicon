#import <Cocoa/Cocoa.h>
#import "VIMUpdateChecker.h"

static NSInteger status = 200;
static NSData *body;
static NSUInteger requests;
@interface MockGitHub : NSURLProtocol @end
@implementation MockGitHub
+ (BOOL)canInitWithRequest:(NSURLRequest *)request { return [request.URL.host isEqual:@"api.github.com"]; }
+ (NSURLRequest *)canonicalRequestForRequest:(NSURLRequest *)request { return request; }
- (void)startLoading {
    requests++;
    NSHTTPURLResponse *response = [[NSHTTPURLResponse alloc] initWithURL:self.request.URL
        statusCode:status HTTPVersion:@"HTTP/1.1" headerFields:@{@"Content-Type":@"application/json"}];
    [self.client URLProtocol:self didReceiveResponse:response cacheStoragePolicy:NSURLCacheStorageNotAllowed];
    [self.client URLProtocol:self didLoadData:body];
    [self.client URLProtocolDidFinishLoading:self];
}
- (void)stopLoading {}
@end
@interface TestChecker : VIMUpdateChecker
@property NSString *title;
@end
@implementation TestChecker
- (void)showTitle:(NSString *)title detail:(NSString *)detail release:(BOOL)release { self.title = title; }
@end
static void require(BOOL condition, NSString *description) {
    if (!condition) { fprintf(stderr, "FAIL: %s\n", description.UTF8String); exit(1); }
}
int main(void) { @autoreleasepool {
    for (id bad in @[@"", @"1.2-beta", @"1.2\n", @"1..2", @"-1", @"1.2.3.4.5", @"9999999999", @12, NSNull.null])
        require([VIMUpdateChecker stableVersion:bad] == nil, [NSString stringWithFormat:@"reject %@", bad]);
    require([[VIMUpdateChecker stableVersion:@"v1.2.3"] isEqual:@"1.2.3"], @"v prefix");
    require([VIMUpdateChecker compareVersion:@"1.10" toVersion:@"1.9"] == NSOrderedDescending, @"numeric comparison");
    require([VIMUpdateChecker compareVersion:@"1.0.0" toVersion:@"1"] == NSOrderedSame, @"padding");
    require([VIMUpdateChecker compareVersion:@"0.9" toVersion:@"1"] == NSOrderedAscending, @"older");
    [NSURLProtocol registerClass:MockGitHub.class];
    [NSUserDefaults.standardUserDefaults setBool:YES forKey:@"VIMAutomaticUpdateChecks"];
    NSArray *cases = @[
        @[@200, @"{\"tag_name\":\"v2.0\",\"draft\":false,\"prerelease\":false}", @"A New Version Is Available"],
        @[@200, @"{\"tag_name\":\"v1.0\",\"draft\":false,\"prerelease\":false}", @"You’re Up to Date"],
        @[@200, @"{\"tag_name\":\"v0.9\",\"draft\":false,\"prerelease\":false}", @"You’re Up to Date"],
        @[@200, @"{\"tag_name\":\"v2.0\",\"draft\":false,\"prerelease\":true}", @"Unable to Check for Updates"],
        @[@200, @"[]", @"Unable to Check for Updates"],
        @[@200, @"{\"tag_name\":null}", @"Unable to Check for Updates"],
        @[@404, @"{}", @"Unable to Check for Updates"],
        @[@429, @"{}", @"Unable to Check for Updates"]];
    for (NSArray *item in cases) {
        status = [item[0] integerValue]; body = [item[1] dataUsingEncoding:NSUTF8StringEncoding];
        TestChecker *checker = [TestChecker new]; [checker checkManually:YES];
        NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:5];
        while (checker.checking && deadline.timeIntervalSinceNow > 0)
            [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
        require(!checker.checking && [checker.title isEqual:item[2]], [NSString stringWithFormat:@"HTTP/JSON %@", item]);
    }
    NSUInteger previous = requests;
    TestChecker *checker = [TestChecker new]; [checker checkManually:NO];
    require(!checker.checking && requests == previous, @"daily automatic throttle");
    puts("PASS: version parsing, eight mocked API responses, daily throttle; no network or UI alerts");
    return 0;
} }
