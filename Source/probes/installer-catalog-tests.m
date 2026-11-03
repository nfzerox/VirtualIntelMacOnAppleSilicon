#import <Foundation/Foundation.h>
#import "VIMInstallerCatalog.h"
#import "VIMInstallerSource.h"
static void check(BOOL ok,NSString *message) { if(!ok){NSLog(@"FAIL: %@",message);exit(1);} }
static NSData *embedded,*remote;
static NSURL *cacheURL;
@interface FixtureCatalog : VIMInstallerCatalog @end
@implementation FixtureCatalog
+ (NSData *)bundledData {return embedded;}
+ (NSURL *)cacheURL {return cacheURL;}
@end
@interface MockCatalog : NSURLProtocol @end
@implementation MockCatalog
+ (BOOL)canInitWithRequest:(NSURLRequest *)request {return [request.URL.host isEqual:@"raw.githubusercontent.com"];}
+ (NSURLRequest *)canonicalRequestForRequest:(NSURLRequest *)request {return request;}
- (void)startLoading {
    [self.client URLProtocol:self didReceiveResponse:[[NSHTTPURLResponse alloc] initWithURL:self.request.URL statusCode:200 HTTPVersion:@"HTTP/1.1" headerFields:nil] cacheStoragePolicy:NSURLCacheStorageNotAllowed];
    [self.client URLProtocol:self didLoadData:remote];[self.client URLProtocolDidFinishLoading:self];
}
- (void)stopLoading {}
@end
static NSData *Revision(NSData *data,id revision) {
    NSMutableDictionary *plist=[NSPropertyListSerialization propertyListWithData:data options:NSPropertyListMutableContainersAndLeaves format:nil error:NULL];
    plist[@"Revision"]=revision;
    return [NSPropertyListSerialization dataWithPropertyList:plist format:NSPropertyListXMLFormat_v1_0 options:0 error:NULL];
}
static void Refresh(NSUInteger expected) {
    __block BOOL complete=NO;
    [FixtureCatalog refreshWithCompletion:^(VIMInstallerCatalog *catalog,NSError *error){
        check(!error&&catalog.revision==expected,@"refresh never returns an older catalog");complete=YES;
    }];
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:5];
    while(!complete&&deadline.timeIntervalSinceNow>0)[NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    check(complete,@"bounded mock refresh");
}
int main(int argc,char **argv) { @autoreleasepool {
    if(argc!=2)return 64;
    NSData *data=[NSData dataWithContentsOfFile:@(argv[1])];NSError *error=nil;
    VIMInstallerCatalog *catalog=[VIMInstallerCatalog catalogWithData:data error:&error];
    NSArray *expectedGuests=@[@"tiger",@"leopard",@"snowleopard",@"lion",@"mountainlion",@"mavericks",@"yosemite",@"elcapitan",@"sierra",@"highsierra",@"mojave",@"catalina",@"bigsur"];
    check([[catalog.supportedInstallers valueForKey:@"Guest"] isEqual:expectedGuests],@"thirteen supported guests in chronological order");
    NSMutableDictionary *bad=[NSPropertyListSerialization propertyListWithData:data options:NSPropertyListMutableContainersAndLeaves format:nil error:NULL];
    bad[@"Installers"][0][@"Files"][0][@"URL"]=@"https://example.org/untrusted.pkg";
    NSData *badData=[NSPropertyListSerialization dataWithPropertyList:bad format:NSPropertyListXMLFormat_v1_0 options:0 error:NULL];
    check(![VIMInstallerCatalog catalogWithData:badData error:NULL],@"reject unapproved payload");
    NSURL *tiger=[NSURL URLWithString:catalog.supportedInstallers[0][@"Files"][0][@"URL"]];
    check([VIMInstallerSource isApprovedURL:tiger]&&[VIMInstallerSource isRestoreDVDAsset:tiger forGuest:@"tiger"],@"the supplied Tiger restore item uses the common downloader");
    check(![VIMInstallerSource isRestoreDVDAsset:tiger forGuest:@"leopard"],@"Tiger cannot be mistaken for Leopard");
    NSURL *snow=[NSURL URLWithString:catalog.supportedInstallers[2][@"Files"][0][@"URL"]];
    check([VIMInstallerSource isSnowLeopardRestoreAsset:snow],@"only the supplied Snow Leopard restore item is approved");
    NSURL *leopard=[NSURL URLWithString:@"https://archive.org/download/10.5.7-9-j-3050/10.5.7-9J3050.dmg"];
    check([VIMInstallerSource isApprovedURL:leopard]&&[VIMInstallerSource isRestoreDVDAsset:leopard forGuest:@"leopard"],@"the supplied Leopard restore item uses the common downloader");
    check(![VIMInstallerSource isSnowLeopardRestoreAsset:leopard]&&![VIMInstallerSource isRestoreDVDAsset:snow forGuest:@"leopard"],@"restore versions cannot be interchanged");
    check(![VIMInstallerSource isRestoreDVDAsset:[NSURL URLWithString:@"https://archive.org/download/10.5.7-9-j-3050/other.dmg"] forGuest:@"leopard"],@"unrecognized restore payload refused");
    NSError *offline=[NSError errorWithDomain:NSURLErrorDomain code:NSURLErrorNotConnectedToInternet userInfo:nil];
    NSError *http=[NSError errorWithDomain:@"VirtualIntelMac.Download" code:2 userInfo:nil];
    for(NSString *zone in @[@"Asia/Shanghai",@"Asia/Chongqing",@"Asia/Chungking",@"Asia/Harbin",@"Asia/Urumqi",@"Asia/Kashgar",@"PRC"])
        check([VIMInstallerSource shouldExplainArchiveFailure:offline source:snow guest:@"snowleopard" region:@"US" timeZone:zone],@"mainland timezone guidance");
    for(NSString *guest in @[@"tiger",@"leopard",@"snowleopard"])
        check([VIMInstallerSource shouldExplainArchiveFailure:http source:leopard guest:guest region:@"CN" timeZone:@"America/Chicago"],@"mainland region guidance for HTTP failure");
    check(![VIMInstallerSource shouldExplainArchiveFailure:offline source:snow guest:@"snowleopard" region:@"HK" timeZone:@"Asia/Hong_Kong"],@"Hong Kong is not classified as mainland");
    check(![VIMInstallerSource shouldExplainArchiveFailure:offline source:snow guest:@"snowleopard" region:@"TW" timeZone:@"Asia/Taipei"],@"Taiwan is not classified as mainland");
    check(![VIMInstallerSource shouldExplainArchiveFailure:offline source:[NSURL URLWithString:@"https://updates.cdn-apple.com/InstallMacOSX.dmg"] guest:@"lion" region:@"CN" timeZone:@"Asia/Shanghai"],@"Apple downloads retain their normal errors");
    for(NSError *local in @[[NSError errorWithDomain:NSPOSIXErrorDomain code:28 userInfo:nil],
        [NSError errorWithDomain:NSCocoaErrorDomain code:NSFileWriteOutOfSpaceError userInfo:nil],
        [NSError errorWithDomain:@"VirtualIntelMac.Download" code:1 userInfo:nil],
        [NSError errorWithDomain:NSURLErrorDomain code:NSURLErrorCancelled userInfo:nil],
        [NSError errorWithDomain:NSURLErrorDomain code:NSURLErrorCannotWriteToFile userInfo:nil]])
        check(![VIMInstallerSource shouldExplainArchiveFailure:local source:snow guest:@"snowleopard" region:@"CN" timeZone:@"Asia/Shanghai"],@"local file, state and cancellation errors are never regional network errors");
    check(![VIMInstallerCatalog catalogWithData:[NSData dataWithBytes:"bad" length:3] error:NULL],@"reject malformed plist");
    for(id invalid in @[@YES,@0,@(-1),@1.5,@"2"])
        check(![VIMInstallerCatalog catalogWithData:Revision(data,invalid) error:NULL],@"revision must be a positive integer");
    NSString *temporary=[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
    check([NSFileManager.defaultManager createDirectoryAtPath:temporary withIntermediateDirectories:NO attributes:nil error:NULL],@"private fixture directory");
    cacheURL=[NSURL fileURLWithPath:[temporary stringByAppendingPathComponent:@"Catalog.plist"]];
    embedded=Revision(data,@10);[Revision(data,@9) writeToURL:cacheURL atomically:YES];
    check([FixtureCatalog currentCatalogWithError:NULL].revision==10,@"newer embedded copy wins over cache");
    [NSURLProtocol registerClass:MockCatalog.class];remote=Revision(data,@8);Refresh(10);
    check([[NSData dataWithContentsOfURL:cacheURL] isEqual:Revision(data,@9)],@"older remote does not replace cache");
    remote=Revision(data,@11);Refresh(11);
    check([FixtureCatalog currentCatalogWithError:NULL].revision==11,@"newer remote persists");
    NSData *newCache=[NSData dataWithContentsOfURL:cacheURL];remote=Revision(data,@10);Refresh(11);
    check([[NSData dataWithContentsOfURL:cacheURL] isEqual:newCache],@"late older response cannot downgrade cache");
    remote=Revision(data,@11);Refresh(11);
    check([[NSData dataWithContentsOfURL:cacheURL] isEqual:newCache],@"equal revision preserves cache");
    [NSURLProtocol unregisterClass:MockCatalog.class];
    [NSFileManager.defaultManager removeItemAtPath:temporary error:NULL];
    NSURL *apple=[NSURL URLWithString:catalog.supportedInstallers[0][@"Files"][0][@"URL"]];
    NSUserDefaults *defaults=NSUserDefaults.standardUserDefaults;
    // Volatile argument domain: never alter the developer's persisted app prefs.
    [defaults setVolatileDomain:@{@"VIMDebugMode":@YES,@"VIMDownloadServer":@"http://127.0.0.1:8765"} forName:NSArgumentDomain];
    NSURL *local=[VIMInstallerCatalog downloadURLForAppleURL:apple];
    check([local.host isEqual:@"127.0.0.1"] && [local.path isEqual:apple.path],@"loopback retains Apple path");
    [defaults setVolatileDomain:@{@"VIMDebugMode":@NO,@"VIMDownloadServer":@"http://127.0.0.1:8765"} forName:NSArgumentDomain];
    check([[VIMInstallerCatalog downloadURLForAppleURL:apple] isEqual:apple],@"override disabled outside debug");
    puts("PASS: catalog schema, integer revision, newer embedded/cache/remote selection, older/equal refresh refusal, thirteen-guest catalog, source URL restriction, debug override; no network or real cache writes");return 0;
} }
