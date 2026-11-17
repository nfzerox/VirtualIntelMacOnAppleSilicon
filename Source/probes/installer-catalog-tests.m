#import <Foundation/Foundation.h>
#import "VIMInstallerCatalog.h"
#import "VIMInstallerSource.h"
#import "VIMGuestFamilies.h"
static void check(BOOL ok,NSString *message) { if(!ok){NSLog(@"FAIL: %@",message);exit(1);} }
static NSData *embedded,*remote;
static NSURL *cacheURL;
static BOOL mockAssistantEnabled;
@interface VIMInstallerCatalog (TestSessionConfiguration)
+ (NSURLSessionConfiguration *)softwareUpdateSessionConfiguration;
@end
@interface FixtureCatalog : VIMInstallerCatalog @end
@implementation FixtureCatalog
+ (NSData *)bundledData {return embedded;}
+ (NSURL *)cacheURL {return cacheURL;}
+ (NSURLSessionConfiguration *)softwareUpdateSessionConfiguration {
    NSURLSessionConfiguration *config=[super softwareUpdateSessionConfiguration];
    if(mockAssistantEnabled)config.protocolClasses=@[NSClassFromString(@"MockAssistantCatalog")];
    return config;
}
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
static NSDictionary<NSString *,NSData *> *assistantResponses;
static NSMutableArray<NSString *> *assistantRequests;
@interface MockAssistantCatalog : NSURLProtocol @end
@implementation MockAssistantCatalog
+ (BOOL)canInitWithRequest:(NSURLRequest *)request {
    return [@[@"swscan.apple.com",@"swdist.apple.com"] containsObject:request.URL.host];
}
+ (NSURLRequest *)canonicalRequestForRequest:(NSURLRequest *)request {return request;}
- (void)startLoading {
    NSString *URL=self.request.URL.absoluteString;
    @synchronized(assistantRequests){[assistantRequests addObject:URL];}
    NSData *data=assistantResponses[URL];
    [self.client URLProtocol:self didReceiveResponse:[[NSHTTPURLResponse alloc] initWithURL:self.request.URL
        statusCode:data?200:404 HTTPVersion:@"HTTP/1.1" headerFields:nil] cacheStoragePolicy:NSURLCacheStorageNotAllowed];
    if(data)[self.client URLProtocol:self didLoadData:data];
    [self.client URLProtocolDidFinishLoading:self];
}
- (void)stopLoading {}
@end

static NSDictionary *ResolveAssistant(NSString *guest,NSError **issue) {
    __block BOOL complete=NO;__block NSDictionary *result=nil;__block NSError *failure=nil;
    [FixtureCatalog resolveAssistantForGuest:guest completion:^(NSDictionary *file,NSError *error){
        check(NSThread.isMainThread,@"assistant completion stays on the main queue");
        result=file;failure=error;complete=YES;
    }];
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:5];
    while(!complete&&deadline.timeIntervalSinceNow>0)
        [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.01]];
    check(complete,@"bounded mock assistant resolution");
    if(issue)*issue=failure;
    return result;
}

static void TestAssistantResolution(NSData *data) {
    embedded=data;cacheURL=[NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString]];
    assistantRequests=[NSMutableArray new];
    mockAssistantEnabled=YES;
    [NSURLProtocol registerClass:MockAssistantCatalog.class];
    for(NSString *guest in @[@"bigsur",@"monterey",@"ventura",@"sonoma",@"sequoia",@"tahoe"]) {
        NSDictionary *entry=[FixtureCatalog installerForGuest:guest];
        NSString *major=VIMMajorVersionForGuest(guest),*catalog=entry[@"SoftwareUpdate"][@"CatalogURL"];
        NSMutableDictionary *products=[NSMutableDictionary new],*responses=[NSMutableDictionary new];
        // Synthetic versions exercise numeric ordering and same-version
        // replacement dates, not a claim about any live Apple release.
        for(NSString *identifier in @[@"older",@"newer",@"replacement",@"other-major",@"standalone-app"]) {
            NSString *dist=[NSString stringWithFormat:@"https://swdist.apple.com/content/downloads/fixture/%@.dist",identifier];
            NSString *version=[NSString stringWithFormat:@"%@.8.%@",major,[identifier isEqual:@"older"]?@"9":@"10"];
            if([identifier isEqual:@"other-major"])version=@"99.9";
            NSString *package=[identifier isEqual:@"standalone-app"]?@"Safari.pkg":@"InstallAssistant.pkg";
            NSString *URL=[NSString stringWithFormat:@"https://swcdn.apple.com/content/downloads/fixture/%@/%@",identifier,package];
            products[identifier]=@{@"Packages":@[@{@"URL":URL,@"Size":@123456}],
                @"Distributions":@{@"English":dist},@"PostDate":[NSDate dateWithTimeIntervalSince1970:
                    [identifier isEqual:@"replacement"]?200:100]};
            NSString *XML=[NSString stringWithFormat:@"<installer-gui-script><dict><key>VERSION</key><string>%@</string><key>BUILD</key><string>%@</string></dict></installer-gui-script>",version,identifier];
            responses[dist]=[XML dataUsingEncoding:NSUTF8StringEncoding];
        }
        responses[catalog]=[NSPropertyListSerialization dataWithPropertyList:@{@"Products":products}
            format:NSPropertyListXMLFormat_v1_0 options:0 error:nil];
        assistantResponses=responses;[assistantRequests removeAllObjects];
        NSError *error=nil;NSDictionary *resolved=ResolveAssistant(guest,&error);
        check(!error&&[resolved[@"Product"] isEqual:@"replacement"]&&[resolved[@"Build"] isEqual:@"replacement"]&&
            [resolved[@"Version"] isEqual:[major stringByAppendingString:@".8.10"]]&&
            [resolved[@"Size"] isEqual:@123456]&&![resolved[@"CatalogFallback"] boolValue],
            [NSString stringWithFormat:@"%@ selects newest full OS installer: %@ (%@)",guest,resolved,error]);
        check([assistantRequests containsObject:catalog],@"a fixed Big Sur source does not replace live catalog resolution");
        // Unavailable optional metadata keeps only an explicitly configured
        // known base, rather than inventing a URL for newer releases.
        assistantResponses=@{};
        resolved=ResolveAssistant(guest,&error);
        if([entry[@"Files"] count])check(!error&&[resolved[@"CatalogFallback"] boolValue]&&
            [resolved[@"URL"] isEqual:entry[@"Files"][0][@"URL"]],@"catalog unavailability retains the configured genuine base");
        else check(!resolved&&error!=nil,@"without a known base, unavailable catalog metadata remains actionable");
    }
    [NSURLProtocol unregisterClass:MockAssistantCatalog.class];
    mockAssistantEnabled=NO;
    assistantResponses=nil;assistantRequests=nil;
}

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
    BOOL updates=argc==4&&!strcmp(argv[1],"--resolve-updates");
    if(argc==4&&(!strcmp(argv[1],"--resolve")||updates)){
        embedded=[NSData dataWithContentsOfFile:@(argv[2])];
        cacheURL=[NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString]];
        __block BOOL complete=NO;
        __block id resolved=nil;
        if(updates)[FixtureCatalog resolveHostUpdatesForGuest:@(argv[3]) completion:^(NSArray *files,NSError *error){
            if(error)fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);
            resolved=files;complete=YES;
        }];
        else [FixtureCatalog resolveAssistantForGuest:@(argv[3]) completion:^(NSDictionary *file,NSError *error){
            if(error)fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);
            resolved=file;complete=YES;
        }];
        while(!complete)[NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.1]];
        if(!resolved)return 1;
        NSData *json=[NSJSONSerialization dataWithJSONObject:resolved options:NSJSONWritingPrettyPrinted error:NULL];
        [NSFileHandle.fileHandleWithStandardOutput writeData:json];
        puts("");
        return 0;
    }
    if(argc!=2)return 64;
    NSData *data=[NSData dataWithContentsOfFile:@(argv[1])];NSError *error=nil;
    NSDictionary *root=[NSPropertyListSerialization propertyListWithData:data options:0 format:nil error:&error];
    check([[root[@"Installers"] valueForKey:@"Guest"] isEqual:VIMSupportedGuests()],@"one flat installer list covers every supported guest");
    VIMInstallerCatalog *catalog=[VIMInstallerCatalog catalogWithData:data error:&error];
    check([[catalog.supportedInstallers valueForKey:@"Guest"] isEqual:VIMSupportedGuests()],@"all supported guests in chronological order");
    NSDictionary *recipe=catalog.supportedInstallers.firstObject[@"HostUpdates"];
    NSMutableDictionary *products=[NSMutableDictionary dictionary];
    for(NSDictionary *step in recipe[@"Steps"])products[step[@"Product"]]=@{@"Packages":@[
        @{@"URL":[@"http://swcdn.apple.com/content/downloads/fixture/" stringByAppendingString:step[@"Package"]],@"Size":@4096}]};
    NSArray *updatesFiles=[VIMInstallerCatalog hostUpdateFilesForRecipe:recipe catalog:@{@"Products":products} error:&error];
    check(updatesFiles.count==2&&[updatesFiles[0][@"HostUpdateRole"] isEqual:@"Combo"]&&
        [updatesFiles[1][@"HostUpdateRole"] isEqual:@"Security"]&&
        [updatesFiles[0][@"URL"] hasPrefix:@"https://swcdn.apple.com/"]&&[updatesFiles[0][@"Optional"] isEqual:@YES],
        @"native optional updates retain recipe order and upgrade old Apple HTTP URLs to HTTPS");
    check([VIMInstallerSource isApprovedURL:[NSURL URLWithString:updatesFiles[0][@"URL"]]]&&
        ![VIMInstallerSource isApprovedURL:[NSURL URLWithString:@"https://example.com/content/downloads/update.tar"]]&&
        ![VIMInstallerSource isApprovedURL:[NSURL URLWithString:@"http://swcdn.apple.com/content/downloads/update.tar"]],
        @"signed period update tarballs use the resumable Apple transport, without relaxing publisher or TLS");
    NSMutableDictionary *missing=[products mutableCopy];[missing removeObjectForKey:recipe[@"Steps"][1][@"Product"]];
    check(![VIMInstallerCatalog hostUpdateFilesForRecipe:recipe catalog:@{@"Products":missing} error:nil],
        @"a missing security hop cannot return a partial recipe");
    NSMutableDictionary *wrong=[products mutableCopy];NSDictionary *first=recipe[@"Steps"][0];
    wrong[first[@"Product"]]=@{@"Packages":@[@{@"URL":[@"https://example.com/content/downloads/" stringByAppendingString:first[@"Package"]],@"Size":@4096}]};
    check(![VIMInstallerCatalog hostUpdateFilesForRecipe:recipe catalog:@{@"Products":wrong} error:nil],
        @"optional update payloads must remain on Apple's publisher");
    NSMutableDictionary *reversed=[recipe mutableCopy];reversed[@"Steps"]=[[recipe[@"Steps"] reverseObjectEnumerator] allObjects];
    check(![VIMInstallerCatalog hostUpdateFilesForRecipe:reversed catalog:@{@"Products":products} error:nil],
        @"security updates cannot precede their base-version prerequisite");
    NSDictionary *leopardRecipe=catalog.supportedInstallers[[VIMSupportedGuests() indexOfObject:@"leopard"]][@"HostUpdates"];
    NSMutableDictionary *leopardProducts=[NSMutableDictionary new];
    for(NSDictionary *step in leopardRecipe[@"Steps"])leopardProducts[step[@"Product"]]=@{@"Packages":@[
        @{@"URL":[@"http://swcdn.apple.com/content/downloads/fixture/" stringByAppendingString:step[@"Package"]],@"Size":@8192}]};
    NSArray *leopardFiles=[VIMInstallerCatalog hostUpdateFilesForRecipe:leopardRecipe catalog:@{@"Products":leopardProducts} error:nil];
    check(leopardFiles.count==2&&[leopardFiles[0][@"URL"] hasSuffix:@"/MacOSXUpdCombo10.5.8.pkg"]&&
          [leopardFiles[1][@"URL"] hasSuffix:@"/SecUpd2011-004.pkg"],@"Leopard selects only its complete client OS update chain");
    NSDictionary *snowRecipe=catalog.supportedInstallers[[VIMSupportedGuests() indexOfObject:@"snowleopard"]][@"HostUpdates"];
    NSMutableArray *snowPackages=[NSMutableArray new];
    for(NSDictionary *step in snowRecipe[@"Steps"])[snowPackages addObject:
        @{@"URL":[@"http://swcdn.apple.com/content/downloads/fixture/" stringByAppendingString:step[@"Package"]],@"Size":@8192}];
    NSMutableDictionary *snowProducts=[NSMutableDictionary new];
    for(NSDictionary *step in snowRecipe[@"Steps"])snowProducts[step[@"Product"]]=@{@"Packages":snowPackages};
    NSArray *snowFiles=[VIMInstallerCatalog hostUpdateFilesForRecipe:snowRecipe catalog:@{@"Products":snowProducts} error:nil];
    check(snowFiles.count==17&&[snowFiles[0][@"HostUpdateRole"] isEqual:@"ComboPart0"]&&
          [snowFiles.lastObject[@"HostUpdateRole"] isEqual:@"Security"],@"Snow's multipart OS sequence stays ordered and complete");
    [snowPackages removeObjectAtIndex:6];
    check(![VIMInstallerCatalog hostUpdateFilesForRecipe:snowRecipe catalog:@{@"Products":snowProducts} error:nil],
        @"an unavailable middle OS part cannot produce a partial update sequence");
    check(![VIMInstallerCatalog hostUpdateFilesForRecipe:@{} catalog:@{} error:nil],@"missing optional recipe fields keep the base instead of throwing");
    NSDictionary *lionRecipe=catalog.supportedInstallers[[VIMSupportedGuests() indexOfObject:@"lion"]][@"HostUpdates"];
    NSDictionary *lionProduct=@{@"zzz041-84761":@{@"Packages":@[@{@"URL":
        @"http://swcdn.apple.com/content/downloads/fixture/SecUpd2014-004Lion.pkg",@"Size":@144444137}]}};
    NSArray *lionFiles=[VIMInstallerCatalog hostUpdateFilesForRecipe:lionRecipe catalog:@{@"Products":lionProduct} error:nil];
    check(lionFiles.count==1&&[lionFiles[0][@"HostUpdateRole"] isEqual:@"Security"]&&
          [lionFiles[0][@"URL"] hasSuffix:@"/SecUpd2014-004Lion.pkg"],@"Lion's 10.7.5 base needs only its inventoried client OS security package");
    check(![VIMInstallerCatalog hostUpdateFilesForRecipe:lionRecipe catalog:@{@"Products":@{}} error:nil],
          @"a missing Lion security package falls back to the verified base");
    NSDictionary *mountainRecipe=catalog.supportedInstallers[[VIMSupportedGuests() indexOfObject:@"mountainlion"]][@"HostUpdates"];
    NSDictionary *mountainProduct=@{@"zzz041-88224":@{@"Packages":@[@{@"URL":
        @"http://swcdn.apple.com/content/downloads/fixture/SecUpd2015-006MtLion.pkg",@"Size":@216397529}]}};
    NSArray *mountainFiles=[VIMInstallerCatalog hostUpdateFilesForRecipe:mountainRecipe catalog:@{@"Products":mountainProduct} error:nil];
    check(mountainFiles.count==1&&[mountainFiles[0][@"HostUpdateRole"] isEqual:@"Security"]&&
          [mountainFiles[0][@"URL"] hasSuffix:@"/SecUpd2015-006MtLion.pkg"],@"Mountain Lion's catalog base selects its final client OS security package");
    check(![VIMInstallerCatalog hostUpdateFilesForRecipe:mountainRecipe catalog:@{@"Products":@{}} error:nil],
          @"missing Mountain Lion security media keeps the verified base");
    NSDictionary *mavericksRecipe=catalog.supportedInstallers[[VIMSupportedGuests() indexOfObject:@"mavericks"]][@"HostUpdates"];
    NSDictionary *mavericksProduct=@{@"zzz041-88446":@{@"Packages":@[@{@"URL":
        @"http://swcdn.apple.com/content/downloads/fixture/SecUpd2016-004Mavericks.pkg",@"Size":@370988463}]}};
    NSArray *mavericksFiles=[VIMInstallerCatalog hostUpdateFilesForRecipe:mavericksRecipe catalog:@{@"Products":mavericksProduct} error:nil];
    check(mavericksFiles.count==1&&[mavericksFiles[0][@"HostUpdateRole"] isEqual:@"Security"]&&
          [mavericksFiles[0][@"URL"] hasSuffix:@"/SecUpd2016-004Mavericks.pkg"],@"Mavericks' 13F34 base selects only the final OS security update");
    check(![VIMInstallerCatalog hostUpdateFilesForRecipe:mavericksRecipe catalog:@{@"Products":@{}} error:nil],
          @"missing Mavericks security media keeps the verified base");
    NSDictionary *yosemiteRecipe=catalog.supportedInstallers[[VIMSupportedGuests() indexOfObject:@"yosemite"]][@"HostUpdates"];
    NSDictionary *yosemiteProduct=@{@"041-89045":@{@"Packages":@[
        @{@"URL":@"http://swcdn.apple.com/content/downloads/fixture/SecUpd2017-003Yosemite.pkg",@"Size":@404174255},
        @{@"URL":@"http://swcdn.apple.com/content/downloads/fixture/FirmwareUpdate.pkg",@"Size":@4851079}]}};
    NSArray *yosemiteFiles=[VIMInstallerCatalog hostUpdateFilesForRecipe:yosemiteRecipe catalog:@{@"Products":yosemiteProduct} error:nil];
    check(yosemiteFiles.count==1&&[yosemiteFiles[0][@"HostUpdateRole"] isEqual:@"Security"]&&
          [yosemiteFiles[0][@"URL"] hasSuffix:@"/SecUpd2017-003Yosemite.pkg"],
          @"Yosemite selects its OS security package, not the catalog's physical-Mac EFI flasher");
    check(![VIMInstallerCatalog hostUpdateFilesForRecipe:yosemiteRecipe catalog:@{@"Products":@{}} error:nil],
          @"missing Yosemite security media keeps the verified base");
    NSDictionary *elCapitanRecipe=catalog.supportedInstallers[[VIMSupportedGuests() indexOfObject:@"elcapitan"]][@"HostUpdates"];
    NSMutableArray *elCapitanPackages=[@[
        @{@"URL":@"http://swcdn.apple.com/content/downloads/fixture/SecUpd2018-004ElCapitan.pkg",@"Size":@517739969},
        @{@"URL":@"http://swcdn.apple.com/content/downloads/fixture/FullBundleUpdate.pkg",@"Size":@3802543},
        @{@"URL":@"http://swcdn.apple.com/content/downloads/fixture/FirmwareUpdate.pkg",@"Size":@147424693}] mutableCopy];
    NSDictionary *elCapitanProduct=@{@"041-90802":@{@"Packages":elCapitanPackages}};
    NSArray *elCapitanFiles=[VIMInstallerCatalog hostUpdateFilesForRecipe:elCapitanRecipe catalog:@{@"Products":elCapitanProduct} error:nil];
    check(elCapitanFiles.count==2&&[elCapitanFiles[0][@"HostUpdateRole"] isEqual:@"Security"]&&
          [elCapitanFiles[1][@"HostUpdateRole"] isEqual:@"FullBundle"],
          @"El Capitan selects the OS security update and its base-Safari companion, not hardware firmware");
    [elCapitanPackages removeObjectAtIndex:1];
    check(![VIMInstallerCatalog hostUpdateFilesForRecipe:elCapitanRecipe catalog:@{@"Products":elCapitanProduct} error:nil],
          @"a missing required companion preserves the verified El Capitan base instead of partial updates");
    NSDictionary *sierraRecipe=catalog.supportedInstallers[[VIMSupportedGuests() indexOfObject:@"sierra"]][@"HostUpdates"];
    NSMutableArray *sierraPackages=[@[
        @{@"URL":@"http://swcdn.apple.com/content/downloads/fixture/SecUpd2019-005Sierra.pkg",@"Size":@653497789},
        @{@"URL":@"http://swcdn.apple.com/content/downloads/fixture/FullBundleUpdate.pkg",@"Size":@23643486},
        @{@"URL":@"http://swcdn.apple.com/content/downloads/fixture/FirmwareUpdate.pkg",@"Size":@200746407},
        @{@"URL":@"http://swcdn.apple.com/content/downloads/fixture/EmbeddedOSFirmware.pkg",@"Size":@59323743}] mutableCopy];
    NSDictionary *sierraProduct=@{@"061-41803":@{@"Packages":sierraPackages}};
    NSArray *sierraFiles=[VIMInstallerCatalog hostUpdateFilesForRecipe:sierraRecipe catalog:@{@"Products":sierraProduct} error:nil];
    check(sierraFiles.count==2&&[sierraFiles[0][@"HostUpdateRole"] isEqual:@"Security"]&&
          [sierraFiles[1][@"HostUpdateRole"] isEqual:@"FullBundle"],
          @"Sierra selects both required OS packages, without EFI or iBridge firmware");
    [sierraPackages removeObjectAtIndex:1];
    check(![VIMInstallerCatalog hostUpdateFilesForRecipe:sierraRecipe catalog:@{@"Products":sierraProduct} error:nil],
          @"an incomplete Sierra OS update keeps the original base");
    NSDictionary *highRecipe=catalog.supportedInstallers[[VIMSupportedGuests() indexOfObject:@"highsierra"]][@"HostUpdates"];
    NSMutableDictionary *highProducts=[NSMutableDictionary new];
    for(NSDictionary *step in highRecipe[@"Steps"]) {
        NSString *product=step[@"Product"];
        if(!highProducts[product])highProducts[product]=[@{@"Packages":[NSMutableArray new]} mutableCopy];
        [highProducts[product][@"Packages"] addObject:@{@"URL":[@"http://swcdn.apple.com/content/downloads/fixture/"
            stringByAppendingString:step[@"Package"]],@"Size":@100}];
    }
    [highProducts[@"001-72525"][@"Packages"] addObject:@{@"URL":@"http://swcdn.apple.com/content/downloads/fixture/FirmwareUpdate.pkg"}];
    [highProducts[@"001-72525"][@"Packages"] addObject:@{@"URL":@"http://swcdn.apple.com/content/downloads/fixture/Safari.pkg"}];
    NSArray *highFiles=[VIMInstallerCatalog hostUpdateFilesForRecipe:highRecipe catalog:@{@"Products":highProducts} error:nil];
    check(highFiles.count==7&&[[highFiles valueForKey:@"HostUpdateRole"] isEqual:
        @[@"HopSecurity",@"HopFullBundle",@"Security",@"FullBundle",@"Boot",@"UpdateEngine",@"Recovery"]],
        @"High Sierra selects its required OS hop, final dependencies and Recovery, never standalone apps or physical firmware");
    [highProducts removeObjectForKey:@"001-48382"];
    check(![VIMInstallerCatalog hostUpdateFilesForRecipe:highRecipe catalog:@{@"Products":highProducts} error:nil],
        @"a final High Sierra delta without its required hop preserves the original base");
    NSDictionary *mojaveRecipe=catalog.supportedInstallers[[VIMSupportedGuests() indexOfObject:@"mojave"]][@"HostUpdates"];
    NSMutableArray *mojavePackages=[NSMutableArray new];
    for(NSDictionary *step in mojaveRecipe[@"Steps"])[mojavePackages addObject:@{
        @"URL":[@"http://swcdn.apple.com/content/downloads/fixture/" stringByAppendingString:step[@"Package"]],@"Size":@100}];
    [mojavePackages addObject:@{@"URL":@"http://swcdn.apple.com/content/downloads/fixture/Safari.pkg"}];
    [mojavePackages addObject:@{@"URL":@"http://swcdn.apple.com/content/downloads/fixture/EmbeddedOSFirmware.pkg"}];
    NSDictionary *mojaveProducts=@{@"071-44390":@{@"Packages":mojavePackages}};
    NSArray *mojaveFiles=[VIMInstallerCatalog hostUpdateFilesForRecipe:mojaveRecipe catalog:@{@"Products":mojaveProducts} error:nil];
    check(mojaveFiles.count==5&&[[mojaveFiles valueForKey:@"HostUpdateRole"] isEqual:
        @[@"Security",@"FullBundle",@"Boot",@"UpdateEngine",@"Recovery"]],
        @"Mojave selects its direct OS update and required dependencies without standalone apps or physical firmware");
    [mojavePackages removeObjectAtIndex:4];
    check(![VIMInstallerCatalog hostUpdateFilesForRecipe:mojaveRecipe catalog:@{@"Products":mojaveProducts} error:nil],
        @"missing Mojave Recovery keeps the verified original system");
    NSDictionary *catalinaRecipe=catalog.supportedInstallers[[VIMSupportedGuests() indexOfObject:@"catalina"]][@"HostUpdates"];
    NSMutableArray *catalinaPackages=[NSMutableArray new];
    for(NSDictionary *step in catalinaRecipe[@"Steps"])[catalinaPackages addObject:@{
        @"URL":[@"http://swcdn.apple.com/content/downloads/fixture/" stringByAppendingString:step[@"Package"]],@"Size":@100}];
    [catalinaPackages addObject:@{@"URL":@"http://swcdn.apple.com/content/downloads/fixture/Safari.pkg"}];
    NSDictionary *catalinaProducts=@{@"047-49516":@{@"Packages":catalinaPackages}};
    NSArray *catalinaFiles=[VIMInstallerCatalog hostUpdateFilesForRecipe:catalinaRecipe catalog:@{@"Products":catalinaProducts} error:nil];
    check(catalinaFiles.count==5&&[[catalinaFiles valueForKey:@"HostUpdateRole"] isEqual:
        @[@"Security",@"FullBundle",@"Boot",@"UpdateEngine",@"Recovery"]],
        @"Catalina selects its final OS update and dependencies, never standalone Safari");
    [catalinaPackages removeObjectAtIndex:0];
    check(![VIMInstallerCatalog hostUpdateFilesForRecipe:catalinaRecipe catalog:@{@"Products":catalinaProducts} error:nil],
        @"incomplete Catalina OS update inputs preserve the original base");
    NSDictionary *monterey=catalog.supportedInstallers[[VIMSupportedGuests() indexOfObject:@"monterey"]];
    check([monterey[@"SoftwareUpdate"][@"MajorVersion"] isEqual:@"12"]&&![monterey[@"Files"] count],
        @"a live assistant recipe is usable without a fixed package URL");
    NSMutableDictionary *replacement=[NSPropertyListSerialization propertyListWithData:data
        options:NSPropertyListMutableContainersAndLeaves format:nil error:nil];
    for(NSMutableDictionary *entry in replacement[@"Installers"]) {
        if([entry[@"Assembly"] isEqual:@"assistant-pkg"])
            entry[@"SoftwareUpdate"][@"MajorVersion"]=@99;
        for(NSMutableDictionary *file in entry[@"Files"])
            if([file[@"URL"] hasPrefix:@"https:"])file[@"SHA256"]=@"old-optional-pin";
    }
    NSData *replacementData=[NSPropertyListSerialization dataWithPropertyList:replacement
        format:NSPropertyListXMLFormat_v1_0 options:0 error:nil];
    VIMInstallerCatalog *updated=[VIMInstallerCatalog catalogWithData:replacementData error:nil];
    check(updated.supportedInstallers.count==catalog.supportedInstallers.count,
        @"stale optional HTTPS pins and redundant major-version fields do not reject valid recipes");
    NSURL *replacementURL=[NSURL URLWithString:@"https://updates.cdn-apple.com/new-layout/ReplacementInstaller.dmg?revision=2"];
    check([VIMInstallerSource isApprovedURL:replacementURL]&&
        [VIMInstallerSource verifyFile:[NSURL fileURLWithPath:@"/not-read-for-https"] source:replacementURL error:nil],
        @"legitimate HTTPS replacements do not trigger a second whole-download digest pass");
    check(![VIMInstallerSource isApprovedURL:[NSURL URLWithString:@"https://apple.com.example.org/ReplacementInstaller.dmg"]],
        @"Apple CDN flexibility does not trust unrelated domains");
    TestAssistantResolution(replacementData);
    NSMutableDictionary *bad=[NSPropertyListSerialization propertyListWithData:data options:NSPropertyListMutableContainersAndLeaves format:nil error:NULL];
    bad[@"Installers"][0][@"Files"][0][@"URL"]=@"https://example.org/untrusted.pkg";
    NSData *badData=[NSPropertyListSerialization dataWithPropertyList:bad format:NSPropertyListXMLFormat_v1_0 options:0 error:NULL];
    // Entries are validated individually: the tampered release is dropped,
    // its untrusted URL is never offered, and the other releases remain.
    VIMInstallerCatalog *partial=[VIMInstallerCatalog catalogWithData:badData error:NULL];
    BOOL untrusted=NO;
    for(NSDictionary *entry in partial.supportedInstallers)for(NSDictionary *file in entry[@"Files"])
        untrusted|=[file[@"URL"] isEqual:@"https://example.org/untrusted.pkg"];
    check(!untrusted&&![[partial.supportedInstallers valueForKey:@"Guest"] containsObject:bad[@"Installers"][0][@"Guest"]]&&
          partial.supportedInstallers.count==catalog.supportedInstallers.count-1,@"reject unapproved payload");
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
    puts("PASS: catalog schema, integer revision, newer embedded/cache/remote selection, older/equal refresh refusal, live assistant entries, source URL restriction, debug override; no network or real cache writes");return 0;
} }
