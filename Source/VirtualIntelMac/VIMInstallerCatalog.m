#import "VIMInstallerCatalog.h"
static NSError *CatalogError(NSString *text) {
    return [NSError errorWithDomain:@"VirtualIntelMac.Installers" code:1 userInfo:@{NSLocalizedDescriptionKey:text}];
}
@interface VIMInstallerCatalog ()
+ (NSURL *)cacheURL;
+ (NSData *)bundledData;
+ (instancetype)preferCatalog:(VIMInstallerCatalog *)candidate over:(VIMInstallerCatalog *)baseline;
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
+ (instancetype)preferCatalog:(VIMInstallerCatalog *)candidate over:(VIMInstallerCatalog *)baseline {
    // Equal revisions keep the existing/embedded copy. Catalog maintainers
    // must increment Revision whenever URLs or other metadata change.
    return candidate&&(!baseline||candidate.revision>baseline.revision)?candidate:baseline;
}
+ (instancetype)catalogWithData:(NSData *)data error:(NSError **)error {
    if(!data.length||data.length>262144) { if(error)*error=CatalogError(@"The installer catalog is empty or too large.");return nil; }
    id object=[NSPropertyListSerialization propertyListWithData:data options:0 format:nil error:error];
    BOOL valid=[object isKindOfClass:NSDictionary.class] && [object[@"FormatVersion"] isEqual:@1] &&
        [object[@"Revision"] isKindOfClass:NSNumber.class] &&
        CFGetTypeID((__bridge CFTypeRef)object[@"Revision"])==CFNumberGetTypeID() &&
        !CFNumberIsFloatType((__bridge CFNumberRef)object[@"Revision"]) &&
        [object[@"Revision"] compare:@1]!=NSOrderedAscending &&
        [object[@"Revision"] compare:@(NSIntegerMax)]!=NSOrderedDescending &&
        [object[@"Installers"] isKindOfClass:NSArray.class] && [object[@"Installers"] count]<=16;
    NSMutableSet *guests=[NSMutableSet set];NSMutableArray *supported=[NSMutableArray array];
    if(valid)for(id entry in object[@"Installers"]) {
        if(![entry isKindOfClass:NSDictionary.class]) { valid=NO;break; }
        NSString *guest=entry[@"Guest"],*assembly=entry[@"Assembly"];
        if(![@[@"mojave",@"catalina",@"bigsur",@"highsierra"] containsObject:guest?:@""] || [guests containsObject:guest] ||
            ![entry[@"Name"] isKindOfClass:NSString.class] || ![entry[@"Name"] length] || [entry[@"Name"] length]>80 ||
            ![entry[@"Version"] isKindOfClass:NSString.class] || [entry[@"Version"] length]>30 ||
            ![@[@"split-legacy",@"assistant-pkg"] containsObject:assembly?:@""] || ![entry[@"Files"] isKindOfClass:NSArray.class]) { valid=NO;break; }
        NSMutableSet *roles=[NSMutableSet set];
        for(id file in entry[@"Files"]) {
            if(![file isKindOfClass:NSDictionary.class] || ![file[@"URL"] isKindOfClass:NSString.class]) { valid=NO;break; }
            NSURL *url=[NSURL URLWithString:file[@"URL"]];NSString *role=file[@"Role"];
            if(![@[@"assistant",@"recovery",@"esd"] containsObject:role?:@""] || [roles containsObject:role] ||
                ![url.scheme isEqual:@"https"] || ![url.host isEqual:@"swcdn.apple.com"] || url.user || url.password || url.port || url.fragment ||
                ![url.path hasPrefix:@"/content/downloads/"] || ![url.pathExtension isEqual:@"pkg"]) { valid=NO;break; }
            [roles addObject:role];
        }
        NSSet *expected=[NSSet setWithArray:[assembly isEqual:@"split-legacy"]?@[@"assistant",@"recovery",@"esd"]:@[@"assistant"]];
        if(!valid || ![roles isEqual:expected]) { valid=NO;break; }
        [guests addObject:guest];
        // High Sierra cannot be enabled by a remotely supplied flag in v1.
        if(![guest isEqual:@"highsierra"])[supported addObject:entry];
    }
    if(!valid || supported.count!=3) { if(error)*error=CatalogError(@"The installer catalog is invalid or incompatible.");return nil; }
    VIMInstallerCatalog *catalog=[self new];catalog->_revision=[object[@"Revision"] unsignedIntegerValue];
    catalog->_supportedInstallers=[supported copy];return catalog;
}
+ (instancetype)currentCatalogWithError:(NSError **)error {
    NSData *bundled=[self bundledData];
    VIMInstallerCatalog *fallback=bundled?[self catalogWithData:bundled error:error]:nil;
    NSData *cache=[NSData dataWithContentsOfURL:[self cacheURL]];
    VIMInstallerCatalog *cached=cache?[self catalogWithData:cache error:nil]:nil;
    VIMInstallerCatalog *result=[self preferCatalog:cached over:fallback];
    if(result&&error)*error=nil;
    if(!result && error)*error=CatalogError(@"No valid installer catalog is available.");
    return result;
}
+ (void)refreshWithCompletion:(void (^)(VIMInstallerCatalog *,NSError *))completion {
    NSURL *url=[NSURL URLWithString:@"https://raw.githubusercontent.com/nfzerox/VirtualIntelMacOnAppleSilicon/refs/heads/main/Source/VirtualIntelMac/InstallerCatalog.plist"];
    NSMutableURLRequest *request=[NSMutableURLRequest requestWithURL:url cachePolicy:NSURLRequestReloadIgnoringLocalCacheData timeoutInterval:20];
    [request setValue:@"VirtualIntelMac/1" forHTTPHeaderField:@"User-Agent"];
    [[NSURLSession.sharedSession dataTaskWithRequest:request completionHandler:^(NSData *data,NSURLResponse *response,NSError *error){
        VIMInstallerCatalog *catalog=nil;
        if(!error && (![response isKindOfClass:NSHTTPURLResponse.class] || [(NSHTTPURLResponse *)response statusCode]!=200))
            error=CatalogError(@"The installer catalog could not be updated. The bundled catalog is still available.");
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
@end
