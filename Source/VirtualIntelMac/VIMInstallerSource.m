#import "VIMInstallerSource.h"
#import "VIMInstallerCatalog.h"
#import <CommonCrypto/CommonDigest.h>
#import <Security/Security.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

static NSString *Hex(NSData *data) {
    NSMutableString *value=[NSMutableString new];
    const unsigned char *bytes=data.bytes;
    for(NSUInteger i=0;i<data.length;i++)[value appendFormat:@"%02X",bytes[i]];
    return value;
}
static NSData *Unhex(NSString *text) {
    if(!text.length||text.length%2||[text rangeOfCharacterFromSet:[NSCharacterSet characterSetWithCharactersInString:@"0123456789abcdefABCDEF"].invertedSet].location!=NSNotFound)return nil;
    NSMutableData *data=[NSMutableData dataWithLength:text.length/2];unsigned char *bytes=data.mutableBytes;
    for(NSUInteger i=0;i<data.length;i++){unsigned value=0;[[NSScanner scannerWithString:[text substringWithRange:NSMakeRange(i*2,2)]] scanHexInt:&value];bytes[i]=value;}
    return data;
}
static NSData *SHA(NSData *data) {unsigned char digest[CC_SHA256_DIGEST_LENGTH];CC_SHA256(data.bytes,(CC_LONG)data.length,digest);return [NSData dataWithBytes:digest length:sizeof(digest)];}
static BOOL SourceFailure(NSError **error,NSString *message) {
    if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.InstallerSource" code:1 userInfo:@{NSLocalizedDescriptionKey:message}];return NO;
}
// Only small recovery control messages use this blocking worker bridge. Large
// downloads always use VIMDownload's streaming, cancellable persistent transfer.
static NSData *RecoveryRequest(NSURLSession *session,NSMutableURLRequest *request,NSHTTPURLResponse **response,NSError **error) {
    __block NSData *body;__block NSHTTPURLResponse *http;__block NSError *failure;
    dispatch_semaphore_t done=dispatch_semaphore_create(0);
    [[session dataTaskWithRequest:request completionHandler:^(NSData *data,NSURLResponse *reply,NSError *issue){
        body=data;http=[reply isKindOfClass:NSHTTPURLResponse.class]?(id)reply:nil;failure=issue;dispatch_semaphore_signal(done);
    }] resume];
    dispatch_semaphore_wait(done,DISPATCH_TIME_FOREVER);
    if(failure||http.statusCode!=200||body.length>65536){
        SourceFailure(error,@"Apple’s recovery service could not authorize the Mavericks download. Your partial download is saved; try again later.");return nil;
    }
    if(response)*response=http;return body;
}
@implementation VIMInstallerSource
+ (BOOL)shouldExplainArchiveFailure:(NSError *)error source:(NSURL *)URL guest:(NSString *)guest
    region:(NSString *)region timeZone:(NSString *)timeZone {
    if(![@[@"tiger",@"leopard",@"snowleopard"] containsObject:guest]||
       !([URL.host.lowercaseString isEqual:@"archive.org"]||[URL.host.lowercaseString hasSuffix:@".archive.org"]))return NO;
    BOOL mainland=[region.uppercaseString isEqual:@"CN"]||
        [@[@"Asia/Shanghai",@"Asia/Chongqing",@"Asia/Chungking",@"Asia/Harbin",@"Asia/Urumqi",@"Asia/Kashgar",@"PRC"] containsObject:timeZone];
    if(!mainland)return NO;
    if([error.domain isEqual:@"VirtualIntelMac.Download"]&&error.code==2)return YES; // HTTP response failure.
    if(![error.domain isEqual:NSURLErrorDomain])return NO;
    switch(error.code){
        case NSURLErrorTimedOut:case NSURLErrorCannotFindHost:case NSURLErrorCannotConnectToHost:
        case NSURLErrorNetworkConnectionLost:case NSURLErrorDNSLookupFailed:case NSURLErrorResourceUnavailable:
        case NSURLErrorNotConnectedToInternet:case NSURLErrorBadServerResponse:
        case NSURLErrorInternationalRoamingOff:case NSURLErrorCallIsActive:case NSURLErrorDataNotAllowed:
        case NSURLErrorSecureConnectionFailed:case NSURLErrorServerCertificateHasBadDate:
        case NSURLErrorServerCertificateUntrusted:case NSURLErrorServerCertificateHasUnknownRoot:
        case NSURLErrorServerCertificateNotYetValid:return YES;
        default:return NO;
    }
}
+ (BOOL)isSnowLeopardRestoreAsset:(NSURL *)URL {
    return [self isRestoreDVDAsset:URL forGuest:@"snowleopard"];
}
+ (BOOL)isRestoreDVDAsset:(NSURL *)URL forGuest:(NSString *)guest {
    return [@[@"tiger",@"leopard",@"snowleopard"] containsObject:guest]&&
        [[VIMInstallerCatalog fileForGuest:guest role:@"diskimage"][@"URL"] isEqual:URL.absoluteString];
}
+ (BOOL)isMavericksRecoveryAsset:(NSURL *)URL {
    return [URL.scheme isEqual:@"http"]&&[URL.host isEqual:@"oscdn.apple.com"]&&
        [URL.path hasPrefix:@"/content/downloads/"]&&[URL.lastPathComponent isEqual:@"InstallESD.dmg"]&&
        !URL.query&&!URL.user&&!URL.password&&!URL.port&&!URL.fragment;
}
+ (BOOL)isApprovedURL:(NSURL *)URL {
    if(URL.user||URL.password||URL.port||URL.fragment||URL.query)return NO;
    if([self isMavericksRecoveryAsset:URL])return YES;
    if(![URL.scheme isEqual:@"https"])return NO;
    if([URL.host isEqual:@"archive.org"]||[URL.host hasSuffix:@".archive.org"])
        return ([URL.path hasPrefix:@"/download/"]||[URL.path hasPrefix:@"/0/items/"])&&[URL.pathExtension isEqual:@"dmg"];
    if([URL.host isEqual:@"swcdn.apple.com"])return [URL.path hasPrefix:@"/content/downloads/"]&&[URL.pathExtension isEqual:@"pkg"]&&!URL.query;
    if([URL.host isEqual:@"updates.cdn-apple.com"])
        return [@[@"InstallMacOSX.dmg",@"InstallOS.dmg"] containsObject:URL.lastPathComponent];
    return NO;
}
+ (NSDictionary<NSString *,NSString *> *)headersForURL:(NSURL *)URL error:(NSError **)error {
    NSAssert(!NSThread.isMainThread,@"Recovery authorization is worker-only");
    if(![self isApprovedURL:URL]){SourceFailure(error,@"This installer download location is not supported.");return nil;}
    if(![self isMavericksRecoveryAsset:URL])return @{};
    NSURLSessionConfiguration *config=NSURLSessionConfiguration.ephemeralSessionConfiguration;
    config.timeoutIntervalForRequest=30;config.timeoutIntervalForResource=60;
    config.HTTPShouldSetCookies=NO;config.URLCache=nil;
    NSURLSession *session=[NSURLSession sessionWithConfiguration:config];
    NSHTTPURLResponse *response=nil;
    NSDictionary *recovery=[VIMInstallerCatalog installerForURL:URL][@"Recovery"];
    NSURL *sessionURL=[NSURL URLWithString:recovery[@"SessionURL"]?:@""],*payloadURL=[NSURL URLWithString:recovery[@"PayloadURL"]?:@""];
    if(!sessionURL||!payloadURL){[session invalidateAndCancel];SourceFailure(error,@"The installer catalog has no recovery service for this download.");return nil;}
    NSMutableURLRequest *request=[NSMutableURLRequest requestWithURL:sessionURL];
    if(!RecoveryRequest(session,request,&response,error)){[session invalidateAndCancel];return nil;}
    NSString *server=nil;
    for(NSHTTPCookie *cookie in [NSHTTPCookie cookiesWithResponseHeaderFields:response.allHeaderFields forURL:request.URL])
        if([cookie.name isEqual:@"session"])server=cookie.value;
    NSArray *parts=[server componentsSeparatedByString:@"~"];
    NSData *challenge=parts.count==2?Unhex(parts[1]):nil;
    unsigned char random[8];
    if(!challenge.length||SecRandomCopyBytes(kSecRandomDefault,sizeof(random),random)!=errSecSuccess){[session invalidateAndCancel];SourceFailure(error,@"Apple’s recovery service returned an invalid challenge.");return nil;}
    NSData *client=[NSData dataWithBytes:random length:sizeof(random)];
    // Public recovery-compatible identifiers supplied by Mavericks Forever,
    // not this host's hardware identity.
    NSString *board=@"Mac-3CBD00234E554E41",*serial=@"C0243070168G3M91F";
    NSMutableData *key=[client mutableCopy];[key appendData:challenge];[key appendData:Unhex(@"003EE1E6AC14")];
    [key appendData:SHA([[serial stringByAppendingString:board] dataUsingEncoding:NSUTF8StringEncoding])];
    const unsigned char suffix[10]={0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc};[key appendBytes:suffix length:sizeof(suffix)];
    request=[NSMutableURLRequest requestWithURL:payloadURL];
    request.HTTPMethod=@"POST";[request setValue:@"text/plain" forHTTPHeaderField:@"Content-Type"];
    [request setValue:[@"session=" stringByAppendingString:server] forHTTPHeaderField:@"Cookie"];
    request.HTTPBody=[[NSString stringWithFormat:@"cid=%@\nsn=%@\nbid=%@\nk=%@",Hex(client),serial,board,Hex(SHA(key))] dataUsingEncoding:NSUTF8StringEncoding];
    NSData *data=RecoveryRequest(session,request,NULL,error);[session finishTasksAndInvalidate];if(!data)return nil;
    NSString *body=[[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
    NSMutableDictionary *fields=[NSMutableDictionary new];
    for(NSString *line in [body componentsSeparatedByCharactersInSet:NSCharacterSet.newlineCharacterSet]) {
        NSRange colon=[line rangeOfString:@": "];if(colon.location==NSNotFound)continue;
        fields[[line substringToIndex:colon.location]]=[line substringFromIndex:NSMaxRange(colon)];
    }
    NSURLComponents *asset=[NSURLComponents componentsWithString:fields[@"AU"]?:@""];
    // The legacy CDN has no valid HTTPS certificate for oscdn.apple.com.
    // Retain Apple's exact HTTP asset, never disable certificate validation.
    // The configurable catalog's full-payload SHA-256 is mandatory before use.
    NSString *token=fields[@"AT"];
    if(![self isMavericksRecoveryAsset:asset.URL]||![asset.URL isEqual:URL]||!token.length||token.length>8192||
       [token rangeOfCharacterFromSet:NSCharacterSet.controlCharacterSet].location!=NSNotFound) {
        SourceFailure(error,@"Apple’s recovery service did not provide the expected Mavericks installer.");return nil;
    }
    return @{@"Cookie":[@"AssetToken=" stringByAppendingString:token]};
}
+ (BOOL)verifyFile:(NSURL *)file source:(NSURL *)URL error:(NSError **)error {
    NSString *expected=[VIMInstallerCatalog fileForURL:URL][@"SHA256"];
    if([self isMavericksRecoveryAsset:URL]&&!expected)
        return SourceFailure(error,@"The recovery download has no matching verification record in the installer catalog.");
    if(!expected)return YES;
    int fd=open(file.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0)return SourceFailure(error,@"The downloaded installer could not be read.");
    CC_SHA256_CTX hash;CC_SHA256_Init(&hash);unsigned char bytes[65536];ssize_t n;
    do{n=read(fd,bytes,sizeof(bytes));if(n>0)CC_SHA256_Update(&hash,bytes,(CC_LONG)n);}while(n>0||(n<0&&errno==EINTR));
    close(fd);if(n<0)return SourceFailure(error,@"The downloaded installer could not be verified.");
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];CC_SHA256_Final(digest,&hash);
    if(![Hex([NSData dataWithBytes:digest length:sizeof(digest)]) isEqual:expected.uppercaseString])
        return SourceFailure(error,@"The download did not match the expected Apple installer. It was not used. Remove the saved download and try again.");
    return YES;
}
@end
