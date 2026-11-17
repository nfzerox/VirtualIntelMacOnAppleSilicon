#import "VIMLocalization.h"
#import "VIMInstallerCatalog.h"
#import "VIMInstallerSource.h"
#import "VIMGuestFamilies.h"
#import "../compat/VIMFlatHostUpdateRecipes.h"
/* Catalog-supplied service URLs: HTTPS on the expected host, no credentials,
 * port, query or fragment. The host remains an allowlist; the location is data. */
static NSURL *CatalogServiceURL(id value,NSString *host) {
    if(![value isKindOfClass:NSString.class]||[value length]>2048)return nil;
    NSURL *URL=[NSURL URLWithString:value];
    return [URL.scheme isEqual:@"https"]&&[URL.host isEqual:host]&&!URL.user&&!URL.password&&
        !URL.port&&!URL.query&&!URL.fragment?URL:nil;
}
static BOOL SoftwareUpdateCatalogURL(NSURL *URL);
static NSError *CatalogError(NSString *text) {
    return [NSError errorWithDomain:@"VirtualIntelMac.Installers" code:1 userInfo:@{NSLocalizedDescriptionKey:text}];
}
@interface VIMInstallerCatalog ()
+ (NSURL *)cacheURL;
+ (NSData *)bundledData;
+ (instancetype)preferCatalog:(VIMInstallerCatalog *)candidate over:(VIMInstallerCatalog *)baseline;
+ (NSURLSessionConfiguration *)softwareUpdateSessionConfiguration;
@end
@implementation VIMInstallerCatalog
+ (NSURL *)cacheURL {
    NSURL *support=[NSFileManager.defaultManager URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask].firstObject;
    return [[support URLByAppendingPathComponent:@"Virtual Intel Mac"] URLByAppendingPathComponent:@"InstallerCatalog.plist"];
}
+ (NSData *)bundledData {
    NSURL *resource=[NSBundle.mainBundle URLForResource:@"InstallerCatalog" withExtension:@"plist"];
    return resource?[NSData dataWithContentsOfURL:resource]:nil;
}
static void SortByGuest(NSMutableArray *entries) {
    NSArray *guests=VIMSupportedGuests();
    [entries sortUsingComparator:^NSComparisonResult(NSDictionary *a,NSDictionary *b){
        return [@([guests indexOfObject:a[@"Guest"]]) compare:@([guests indexOfObject:b[@"Guest"]])];
    }];
}
+ (instancetype)preferCatalog:(VIMInstallerCatalog *)candidate over:(VIMInstallerCatalog *)baseline {
    // Equal revisions keep the existing/embedded copy. Catalog maintainers
    // must increment Revision whenever URLs or other metadata change.
    VIMInstallerCatalog *preferred=candidate&&(!baseline||candidate.revision>baseline.revision)?candidate:baseline;
    VIMInstallerCatalog *other=preferred==candidate?baseline:candidate;
    // A release the preferred catalog doesn't list (or that this version
    // couldn't use from it) still comes from the other copy.
    NSSet *listed=[NSSet setWithArray:[preferred.supportedInstallers valueForKey:@"Guest"]?:@[]];
    NSMutableArray *merged=[preferred.supportedInstallers mutableCopy];
    for(NSDictionary *entry in other.supportedInstallers)if(![listed containsObject:entry[@"Guest"]])[merged addObject:entry];
    if(!preferred||merged.count==preferred.supportedInstallers.count)return preferred;
    SortByGuest(merged);
    VIMInstallerCatalog *catalog=[self new];catalog->_revision=preferred.revision;catalog->_updateURL=preferred.updateURL;
    catalog->_supportedInstallers=[merged copy];return catalog;
}
/// One installer entry this app can use: a known release, its approved recipe
/// and approved sources. Anything else is skipped, never fatal, so later
/// catalogs can add releases, keys and recipes without breaking this version.
static BOOL UsableEntry(id entry) {
    if(![entry isKindOfClass:NSDictionary.class])return NO;
    BOOL valid=YES;
    NSString *guest=entry[@"Guest"],*assembly=entry[@"Assembly"];
    if(![VIMSupportedGuests() containsObject:guest?:@""] ||
        ![entry[@"Name"] isKindOfClass:NSString.class] || ![entry[@"Name"] length] || [entry[@"Name"] length]>80 ||
        ![entry[@"Version"] isKindOfClass:NSString.class] || [entry[@"Version"] length]>30 ||
        ![@[@"split-legacy",@"assistant-pkg",@"installer-dmg",@"recovery-esd",@"restore-dvd"] containsObject:assembly?:@""] || ![entry[@"Files"] isKindOfClass:NSArray.class]) return NO;
    NSString *expectedRecipe=@{@"tiger":@"restore-dvd",@"leopard":@"restore-dvd",@"snowleopard":@"restore-dvd",@"lion":@"installer-dmg",@"mountainlion":@"installer-dmg",@"mavericks":@"recovery-esd",@"yosemite":@"installer-dmg",@"elcapitan":@"installer-dmg",@"sierra":@"installer-dmg",@"mojave":@"split-legacy",@"catalina":@"split-legacy",@"highsierra":@"split-legacy"}[guest]?:(VIMIsAssistantGuest(guest)?@"assistant-pkg":nil);
    if(![assembly isEqual:expectedRecipe])return NO;
    // Optional page for "this disc isn't supported"; an approved image URL.
    if(entry[@"ReleaseURL"]&&(![entry[@"ReleaseURL"] isKindOfClass:NSString.class]||
        ![VIMInstallerSource isApprovedURL:[NSURL URLWithString:entry[@"ReleaseURL"]]]))return NO;
    // Apple's recovery authorization endpoints for the Mavericks ESD.
    // Optional for older catalogs; the download requires them.
    if(entry[@"Recovery"]&&(![entry[@"Recovery"] isKindOfClass:NSDictionary.class]||
        !CatalogServiceURL(entry[@"Recovery"][@"SessionURL"],@"osrecovery.apple.com")||
        !CatalogServiceURL(entry[@"Recovery"][@"PayloadURL"],@"osrecovery.apple.com")))return NO;
    // Big Sur and later may name Apple's software update catalog instead
    // of a fixed package URL; the newest installer is resolved live.
    id update=entry[@"SoftwareUpdate"];
    BOOL live=update!=nil;
    if(live&&(![assembly isEqual:@"assistant-pkg"]||![update isKindOfClass:NSDictionary.class]||
        !SoftwareUpdateCatalogURL([NSURL URLWithString:update[@"CatalogURL"]])))return NO;
    NSMutableSet *roles=[NSMutableSet set];
    for(id file in entry[@"Files"]) {
        if(![file isKindOfClass:NSDictionary.class] || ![file[@"URL"] isKindOfClass:NSString.class]) { valid=NO;break; }
        NSURL *url=[NSURL URLWithString:file[@"URL"]];NSString *role=file[@"Role"];
        if(![@[@"assistant",@"recovery",@"esd",@"diskimage"] containsObject:role?:@""] || [roles containsObject:role] ||
            ![VIMInstallerSource isApprovedURL:url]) { valid=NO;break; }
        if([assembly isEqual:@"recovery-esd"]&&![VIMInstallerSource isMavericksRecoveryAsset:url]){valid=NO;break;}
        if([assembly isEqual:@"restore-dvd"]&&![url.host isEqual:@"archive.org"]&&![url.host hasSuffix:@".archive.org"]){valid=NO;break;}
        NSString *hash=file[@"SHA256"];
        if(hash&&![url.scheme.lowercaseString isEqual:@"https"]&&(![hash isKindOfClass:NSString.class]||hash.length!=64||
            [hash rangeOfCharacterFromSet:[NSCharacterSet characterSetWithCharactersInString:@"0123456789abcdefABCDEF"].invertedSet].location!=NSNotFound)){valid=NO;break;}
        if([assembly isEqual:@"recovery-esd"]&&!hash){valid=NO;break;}
        [roles addObject:role];
    }
    NSSet *expected=[NSSet setWithArray:[assembly isEqual:@"split-legacy"]?@[@"assistant",@"recovery",@"esd"]:
        [assembly isEqual:@"recovery-esd"]?@[@"esd"]:[@[@"installer-dmg",@"restore-dvd"] containsObject:assembly]?@[@"diskimage"]:@[@"assistant"]];
    return valid&&((live&&roles.count==0)||[roles isEqual:expected]);
}
+ (instancetype)catalogWithData:(NSData *)data error:(NSError **)error {
    if(!data.length||data.length>262144) { if(error)*error=CatalogError(VIMLocalized(@"installercatalog.the-installer-catalog-is-empty-or-too-large", @"The installer catalog is empty or too large."));return nil; }
    id object=[NSPropertyListSerialization propertyListWithData:data options:0 format:nil error:error];
    BOOL valid=[object isKindOfClass:NSDictionary.class] && [object[@"FormatVersion"] isKindOfClass:NSNumber.class] && [object[@"FormatVersion"] integerValue]>=1 &&
        [object[@"Revision"] isKindOfClass:NSNumber.class] &&
        CFGetTypeID((__bridge CFTypeRef)object[@"Revision"])==CFNumberGetTypeID() &&
        !CFNumberIsFloatType((__bridge CFNumberRef)object[@"Revision"]) &&
        [object[@"Revision"] compare:@1]!=NSOrderedAscending &&
        [object[@"Revision"] compare:@(NSIntegerMax)]!=NSOrderedDescending;
    NSMutableSet *guests=[NSMutableSet set];NSMutableArray *supported=[NSMutableArray array];
    if(valid) {
        NSArray *entries=[object[@"Installers"] isKindOfClass:NSArray.class]?object[@"Installers"]:@[];
        for(id entry in entries.count<=64?entries:@[]) {
            if(!UsableEntry(entry)||[guests containsObject:entry[@"Guest"]])continue;
            [guests addObject:entry[@"Guest"]];[supported addObject:entry];
        }
    }
    SortByGuest(supported);
    if(!valid || !supported.count) { if(error)*error=CatalogError(@"The installer catalog is invalid or incompatible.");return nil; }
    NSURL *update=nil;
    if(object[@"UpdateURL"]&&!(update=CatalogServiceURL(object[@"UpdateURL"],@"raw.githubusercontent.com"))) {
        if(error)*error=CatalogError(VIMLocalized(@"installercatalog.the-installer-catalog-is-invalid-or-incompatible", @"The installer catalog is invalid or incompatible."));return nil;
    }
    VIMInstallerCatalog *catalog=[self new];catalog->_revision=[object[@"Revision"] unsignedIntegerValue];catalog->_updateURL=update;
    catalog->_supportedInstallers=[supported copy];return catalog;
}
+ (instancetype)currentCatalogWithError:(NSError **)error {
    NSData *bundled=[self bundledData];
    VIMInstallerCatalog *fallback=bundled?[self catalogWithData:bundled error:error]:nil;
    NSData *cache=[NSData dataWithContentsOfURL:[self cacheURL]];
    VIMInstallerCatalog *cached=cache?[self catalogWithData:cache error:nil]:nil;
    VIMInstallerCatalog *result=[self preferCatalog:cached over:fallback];
    if(result&&error)*error=nil;
    if(!result && error)*error=CatalogError(VIMLocalized(@"installercatalog.no-valid-installer-catalog-is-available", @"No valid installer catalog is available."));
    return result;
}
+ (NSDictionary *)fileForGuest:(NSString *)guest role:(NSString *)role {
    for(NSDictionary *installer in [self currentCatalogWithError:NULL].supportedInstallers)
        if([installer[@"Guest"] isEqual:guest])
            for(NSDictionary *file in installer[@"Files"])if([file[@"Role"] isEqual:role])return file;
    return nil;
}
+ (NSDictionary *)installerForGuest:(NSString *)guest {
    for(NSDictionary *installer in [self currentCatalogWithError:NULL].supportedInstallers)
        if([installer[@"Guest"] isEqual:guest])return installer;
    return nil;
}
+ (NSDictionary *)installerForURL:(NSURL *)URL {
    for(NSDictionary *installer in [self currentCatalogWithError:NULL].supportedInstallers)
        for(NSDictionary *file in installer[@"Files"])
            if([file[@"URL"] isEqual:URL.absoluteString])return installer;
    return nil;
}
+ (NSDictionary *)fileForURL:(NSURL *)URL {
    for(NSDictionary *installer in [self currentCatalogWithError:NULL].supportedInstallers)
        for(NSDictionary *file in installer[@"Files"])
            if([file[@"URL"] isEqual:URL.absoluteString])return file;
    return nil;
}
+ (void)refreshWithCompletion:(void (^)(VIMInstallerCatalog *,NSError *))completion {
    NSURL *url=[self currentCatalogWithError:NULL].updateURL;
    if(!url) {
        dispatch_async(dispatch_get_main_queue(), ^{
            completion([self currentCatalogWithError:NULL],CatalogError(VIMLocalized(@"installercatalog.the-installer-catalog-has-no-update-location-the-bundled-catalog", @"The installer catalog has no update location. The bundled catalog is still available.")));
        });
        return;
    }
    NSMutableURLRequest *request=[NSMutableURLRequest requestWithURL:url cachePolicy:NSURLRequestReloadIgnoringLocalCacheData timeoutInterval:20];
    [request setValue:@"VirtualIntelMac/1" forHTTPHeaderField:@"User-Agent"];
    [[NSURLSession.sharedSession dataTaskWithRequest:request completionHandler:^(NSData *data,NSURLResponse *response,NSError *error){
        VIMInstallerCatalog *catalog=nil;
        if(!error && (![response isKindOfClass:NSHTTPURLResponse.class] || [(NSHTTPURLResponse *)response statusCode]!=200))
            error=CatalogError(VIMLocalized(@"installercatalog.the-installer-catalog-could-not-be-updated-the-bundled-catalog", @"The installer catalog could not be updated. The bundled catalog is still available."));
        if(!error)catalog=[self catalogWithData:data error:&error];
        // Compare again at publication time, not when starting the request:
        // concurrent refreshes may finish out of order. Never replace a newer
        // cache or return an older remote catalog to an already-open wizard.
        @synchronized(self) {
            VIMInstallerCatalog *current=[self currentCatalogWithError:NULL];
            VIMInstallerCatalog *selected=[self preferCatalog:catalog over:current];
            if(catalog&&selected==catalog) {
                NSURL *cache=[self cacheURL];
                if([NSFileManager.defaultManager createDirectoryAtURL:cache.URLByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:&error])
                    [data writeToURL:cache options:NSDataWritingAtomic error:&error];
            }
            catalog=selected;
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            // Another response may publish between unlocking above and this
            // callback running. Serialize the wizard-facing choice as well.
            VIMInstallerCatalog *latest=[self currentCatalogWithError:NULL];
            completion([self preferCatalog:catalog over:latest],error);
        });
    }] resume];
}
+ (NSURL *)downloadURLForAppleURL:(NSURL *)URL {
    NSUserDefaults *defaults=NSUserDefaults.standardUserDefaults;
    if(![defaults boolForKey:@"VIMDebugMode"])return URL;
    NSURLComponents *override=[NSURLComponents componentsWithString:[defaults stringForKey:@"VIMDownloadServer"]?:@""];
    if(![@[@"127.0.0.1",@"localhost",@"::1"] containsObject:override.host?:@""] ||
        ![override.scheme isEqual:@"http"] || override.user || override.password || override.query || override.fragment)return URL;
    NSURLComponents *original=[NSURLComponents componentsWithURL:URL resolvingAgainstBaseURL:NO];
    override.percentEncodedPath=original.percentEncodedPath;override.percentEncodedQuery=original.percentEncodedQuery;
    return override.URL?:URL;
}

// Apple's software update catalog. Only these hosts are fetched: the
// catalog itself, distribution metadata, and the InstallAssistant package.
static BOOL SoftwareUpdateCatalogURL(NSURL *URL) {
    return [URL.scheme isEqual:@"https"]&&[URL.host isEqual:@"swscan.apple.com"]&&!URL.user&&!URL.password&&!URL.port&&
        !URL.query&&!URL.fragment&&[URL.path hasPrefix:@"/content/catalogs/"]&&[URL.pathExtension isEqual:@"sucatalog"];
}
static BOOL SoftwareUpdateDistributionURL(NSURL *URL) {
    return [URL.scheme isEqual:@"https"]&&[URL.host isEqual:@"swdist.apple.com"]&&!URL.user&&!URL.password&&!URL.port&&
        !URL.query&&!URL.fragment&&[URL.path hasPrefix:@"/content/downloads/"]&&[URL.pathExtension isEqual:@"dist"];
}
static NSArray<NSNumber *> *VersionComponents(NSString *version) {
    NSMutableArray *parts=[NSMutableArray array];
    for(NSString *part in [version componentsSeparatedByString:@"."]){
        if(!part.length||[part rangeOfCharacterFromSet:NSCharacterSet.decimalDigitCharacterSet.invertedSet].location!=NSNotFound)return nil;
        [parts addObject:@(part.integerValue)];
    }
    return parts.count&&parts.count<=4?parts:nil;
}
static NSComparisonResult CompareVersions(NSString *a,NSString *b) {
    NSArray *x=VersionComponents(a),*y=VersionComponents(b);
    for(NSUInteger i=0;i<MAX(x.count,y.count);i++){
        NSInteger p=i<x.count?[x[i] integerValue]:0,q=i<y.count?[y[i] integerValue]:0;
        if(p!=q)return p<q?NSOrderedAscending:NSOrderedDescending;
    }
    return NSOrderedSame;
}
static NSString *DistributionValue(NSString *text,NSString *key) {
    NSString *pattern=[NSString stringWithFormat:@"<key>%@</key>\\s*<string>([^<]{1,40})</string>",key];
    NSRegularExpression *expression=[NSRegularExpression regularExpressionWithPattern:pattern options:0 error:NULL];
    NSTextCheckingResult *match=[expression firstMatchInString:text options:0 range:NSMakeRange(0,text.length)];
    return match?[text substringWithRange:[match rangeAtIndex:1]]:nil;
}
static NSData *FetchCatalogData(NSURLSession *session,NSURL *URL,NSUInteger limit,NSError **error) {
    __block NSData *result=nil;__block NSError *failure=nil;
    dispatch_semaphore_t done=dispatch_semaphore_create(0);
    [[session dataTaskWithURL:[VIMInstallerCatalog downloadURLForAppleURL:URL] completionHandler:^(NSData *data,NSURLResponse *response,NSError *issue){
        NSInteger status=[response isKindOfClass:NSHTTPURLResponse.class]?((NSHTTPURLResponse *)response).statusCode:0;
        if(issue)failure=issue;
        else if(status!=200)failure=CatalogError(@"Apple’s software update catalog is unavailable. Try again later.");
        else if(data.length>limit)failure=CatalogError(@"Apple’s software update catalog is unexpectedly large.");
        else result=data;
        dispatch_semaphore_signal(done);
    }] resume];
    dispatch_semaphore_wait(done,DISPATCH_TIME_FOREVER);
    if(!result&&error)*error=failure;
    return result;
}
+ (NSURLSessionConfiguration *)softwareUpdateSessionConfiguration {
    NSURLSessionConfiguration *config=NSURLSessionConfiguration.ephemeralSessionConfiguration;
    config.timeoutIntervalForRequest=30;config.timeoutIntervalForResource=120;
    config.HTTPShouldSetCookies=NO;config.URLCache=nil;
    return config;
}
+ (void)resolveAssistantForGuest:(NSString *)guest completion:(void (^)(NSDictionary *,NSError *))completion {
    NSDictionary *installer=[self installerForGuest:guest];
    NSDictionary *source=installer[@"SoftwareUpdate"];
    NSURL *catalogURL=[source isKindOfClass:NSDictionary.class]?[NSURL URLWithString:source[@"CatalogURL"]]:nil;
    NSString *major=VIMMajorVersionForGuest(guest);
    if(!SoftwareUpdateCatalogURL(catalogURL)){
        completion(nil,CatalogError(@"This installer isn’t available from Apple’s software update catalog."));return;
    }
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
        NSURLSessionConfiguration *config=[self softwareUpdateSessionConfiguration];
        NSURLSession *session=[NSURLSession sessionWithConfiguration:config];
        NSError *error=nil;NSDictionary *best=nil;
        NSData *data=FetchCatalogData(session,catalogURL,512*1024*1024,&error);
        id catalog=data?[NSPropertyListSerialization propertyListWithData:data options:NSPropertyListImmutable format:NULL error:NULL]:nil;
        NSDictionary *products=[catalog isKindOfClass:NSDictionary.class]?catalog[@"Products"]:nil;
        if(data&&![products isKindOfClass:NSDictionary.class])error=CatalogError(@"Apple’s software update catalog could not be read.");
        for(NSString *identifier in products) {
            NSDictionary *product=products[identifier];
            if(![product isKindOfClass:NSDictionary.class]||![product[@"Packages"] isKindOfClass:NSArray.class])continue;
            NSDictionary *package=nil;
            for(NSDictionary *candidate in product[@"Packages"]) {
                NSURL *URL=[candidate isKindOfClass:NSDictionary.class]?[NSURL URLWithString:candidate[@"URL"]]:nil;
                if([URL.lastPathComponent isEqual:@"InstallAssistant.pkg"]&&[VIMInstallerSource isApprovedURL:URL])package=candidate;
            }
            NSDictionary *distributions=product[@"Distributions"];
            NSURL *distribution=[distributions isKindOfClass:NSDictionary.class]?
                [NSURL URLWithString:distributions[@"English"]?:distributions[@"en"]]:nil;
            if(!package||!SoftwareUpdateDistributionURL(distribution))continue;
            NSData *text=FetchCatalogData(session,distribution,64*1024*1024,NULL);
            NSString *dist=text?[[NSString alloc] initWithData:text encoding:NSUTF8StringEncoding]:nil;
            NSString *version=dist?DistributionValue(dist,@"VERSION"):nil,*build=dist?DistributionValue(dist,@"BUILD"):nil;
            NSArray *parts=VersionComponents(version);
            if(!parts||![[parts.firstObject stringValue] isEqual:major])continue;
            NSDate *posted=[product[@"PostDate"] isKindOfClass:NSDate.class]?product[@"PostDate"]:NSDate.distantPast;
            NSComparisonResult order=best?CompareVersions(version,best[@"Version"]):NSOrderedDescending;
            if(order==NSOrderedDescending||(order==NSOrderedSame&&[posted compare:best[@"PostDate"]]==NSOrderedDescending))
                best=@{@"Role":@"assistant",@"URL":package[@"URL"],@"Version":version,@"Build":build?:@"",
                       @"Product":identifier,@"Size":[package[@"Size"] isKindOfClass:NSNumber.class]?package[@"Size"]:@0,@"PostDate":posted};
        }
        [session finishTasksAndInvalidate];
        NSMutableDictionary *file=[best mutableCopy];[file removeObjectForKey:@"PostDate"];
        // Some catalogs retain a known full installer as an availability
        // fallback (currently Big Sur). Prefer the live version whenever one
        // resolves. The normal package signature/media checks still apply to
        // the fallback; catalog failure never admits an unsigned OS payload.
        if(!file&&[installer[@"Files"] count]==1) {
            NSDictionary *base=[installer[@"Files"] firstObject];
            if([base[@"Role"] isEqual:@"assistant"]) {
                file=[base mutableCopy];file[@"Version"]=installer[@"Version"];
                file[@"CatalogFallback"]=@YES;
            }
        }
        if(!file&&!error)error=CatalogError([NSString stringWithFormat:@"Apple’s software update catalog doesn’t currently list a %@ installer. Try again later.",VIMSystemTitleForGuest(guest)]);
        dispatch_async(dispatch_get_main_queue(),^{completion(file.copy,file?nil:error);});
    });
}
+ (NSArray<NSDictionary *> *)hostUpdateFilesForRecipe:(NSDictionary *)recipe catalog:(NSDictionary *)catalog error:(NSError **)error {
    // Catalog data selects an inventoried operation, never executable scripts.
    NSArray *steps=recipe[@"Steps"];NSDictionary *products=catalog[@"Products"];
    NSString *kind=recipe[@"Recipe"],*extension=nil;NSArray *roles=nil;
    if([kind isEqual:@"tiger-10.4.11-security"]){extension=@".tar";roles=@[@"Combo",@"Security"];}
    else {
        NSString *guest=[@{@"leopard-10.5.8-security":@"leopard",@"snowleopard-10.6.8-security":@"snowleopard",
                          @"lion-10.7.5-security":@"lion",@"mountainlion-10.8.5-security":@"mountainlion",
                          @"mavericks-10.9.5-security":@"mavericks",@"yosemite-10.10.5-security":@"yosemite",
                          @"elcapitan-10.11.6-security":@"elcapitan",@"sierra-10.12.6-security":@"sierra",
                          @"highsierra-10.13.6-security":@"highsierra",@"mojave-10.14.6-security":@"mojave",
                          @"catalina-10.15.8-security":@"catalina"}
            objectForKey:kind?:@""];
        if(guest){extension=@".pkg";roles=[VIMFlatHostUpdateRecipe(guest) valueForKey:@"Role"];}
    }
    if(!extension||![steps isKindOfClass:NSArray.class]||
       steps.count!=roles.count||![products isKindOfClass:NSDictionary.class]) {
        if(error)*error=CatalogError(@"The optional system-update recipe is unavailable. The original installer can still be used.");return nil;
    }
    NSMutableArray *files=[NSMutableArray array];
    for(NSUInteger index=0;index<steps.count;index++) {
        NSDictionary *step=[steps[index] isKindOfClass:NSDictionary.class]?steps[index]:nil;
        NSString *product=step[@"Product"],*name=step[@"Package"];
        NSDictionary *entry=[product isKindOfClass:NSString.class]?products[product]:nil;
        if(![step[@"Role"] isEqual:roles[index]]||![name isKindOfClass:NSString.class]||
           ![name isEqual:name.lastPathComponent]||![name hasSuffix:extension]||![entry isKindOfClass:NSDictionary.class]) {
            if(error)*error=CatalogError(@"Apple’s catalog does not list the complete optional update sequence. The original installer can still be used.");return nil;
        }
        NSDictionary *selected=nil;
        NSArray *packages=[entry[@"Packages"] isKindOfClass:NSArray.class]?entry[@"Packages"]:@[];
        for(id package in packages) {
            if(![package isKindOfClass:NSDictionary.class]||![package[@"URL"] isKindOfClass:NSString.class])continue;
            NSURLComponents *URL=[NSURLComponents componentsWithString:package[@"URL"]];
            if(![URL.URL.lastPathComponent isEqual:name])continue;
            // Tiger prints HTTP, but these identical signed tarballs are
            // available over HTTPS. Never disable transport verification.
            if([URL.scheme isEqual:@"http"])URL.scheme=@"https";
            NSNumber *size=package[@"Size"];
            if(selected||![URL.scheme isEqual:@"https"]||![URL.host isEqual:@"swcdn.apple.com"]||
               URL.user||URL.password||URL.port||URL.query||URL.fragment||
               ![URL.path hasPrefix:@"/content/downloads/"]||
               ![size isKindOfClass:NSNumber.class]||size.longLongValue<=0) {
                if(error)*error=CatalogError(@"Apple’s optional update download could not be identified. The original installer can still be used.");return nil;
            }
            selected=@{@"Role":[@"update-" stringByAppendingString:[roles[index] lowercaseString]],
                @"HostUpdateRole":roles[index],@"Product":product,@"URL":URL.URL.absoluteString,@"Size":size,@"Optional":@YES};
        }
        if(!selected) {
            if(error)*error=CatalogError(@"An optional system update is no longer listed by Apple. The original installer can still be used.");return nil;
        }
        [files addObject:selected];
    }
    return files.copy;
}
+ (void)resolveHostUpdatesForGuest:(NSString *)guest completion:(void (^)(NSArray<NSDictionary *> *,NSError *))completion {
    NSDictionary *recipe=[self installerForGuest:guest][@"HostUpdates"];
    if(!recipe){completion(@[],nil);return;}
    NSURL *URL=[recipe isKindOfClass:NSDictionary.class]&&[recipe[@"CatalogURL"] isKindOfClass:NSString.class]?
        [NSURL URLWithString:recipe[@"CatalogURL"]]:nil;
    if(![@[@"tiger",@"leopard",@"snowleopard",@"lion",@"mountainlion",@"mavericks",@"yosemite",@"elcapitan",@"sierra",@"highsierra",@"mojave",@"catalina"] containsObject:guest]||!SoftwareUpdateCatalogURL(URL)) {
        completion(nil,CatalogError(@"The optional system-update catalog is unavailable. The original installer can still be used."));return;
    }
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
        NSURLSessionConfiguration *config=[self softwareUpdateSessionConfiguration];
        NSURLSession *session=[NSURLSession sessionWithConfiguration:config];NSError *error=nil;
        NSData *data=FetchCatalogData(session,URL,16*1024*1024,&error);
        id catalog=data?[NSPropertyListSerialization propertyListWithData:data options:NSPropertyListImmutable format:NULL error:&error]:nil;
        NSArray *files=[catalog isKindOfClass:NSDictionary.class]?[self hostUpdateFilesForRecipe:recipe catalog:catalog error:&error]:nil;
        if(!files&&!error)error=CatalogError(@"Apple’s optional system-update catalog could not be read. The original installer can still be used.");
        [session finishTasksAndInvalidate];
        dispatch_async(dispatch_get_main_queue(),^{completion(files,error);});
    });
}
@end
