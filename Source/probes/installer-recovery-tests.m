#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#include <stdatomic.h>
#include <unistd.h>
#import "VIMDownload.h"
#import "VIMInstallerCatalog.h"
#import "VIMInstallerSource.h"

static NSString *FixtureHash=@"61D1D9C5745BDAA4FAB39240651BC242A5186B15393FD475082FCF6E84F400AB";
static NSDictionary *FixtureRecord(id object,SEL selector,NSURL *URL) {return @{@"SHA256":FixtureHash};}
static atomic_bool authorizationCancelled;
static atomic_uint authorizationCalls;
static NSString *serverURL;
static NSString *authorizationMode;
static IMP originalVerification;
static BOOL SlowVerification(id object,SEL selector,NSURL *file,NSURL *source,NSError **error) {
    usleep(100000);
    return ((BOOL (*)(id,SEL,NSURL *,NSURL *,NSError **))originalVerification)(object,selector,file,source,error);
}
static NSURL *OriginalMapping(id object,SEL selector,NSURL *URL) {return URL;}
static NSURL *FixtureTransport(id object,SEL selector,NSURL *URL,NSUInteger retry) {
    NSURLComponents *mapped=[NSURLComponents componentsWithString:serverURL];
    if(retry%2)mapped.host=@"localhost";
    mapped.percentEncodedPath=[NSURLComponents componentsWithURL:URL resolvingAgainstBaseURL:NO].percentEncodedPath;
    return mapped.URL;
}
static NSDictionary *FixtureAuthorization(id object,SEL selector,NSURL *URL,BOOL (^cancelled)(void),NSError **error) {
    unsigned call=atomic_fetch_add(&authorizationCalls,1)+1;
    if([authorizationMode isEqual:@"auth-control"]&&call==1) {
        if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.InstallerSource" code:1 userInfo:
            @{VIMInstallerHTTPStatusKey:@503,VIMInstallerRetryAfterKey:[NSDate dateWithTimeIntervalSinceNow:1],NSLocalizedDescriptionKey:@"fixture authorization unavailable"}];
        return nil;
    }
    return @{@"Cookie":call==1?@"AssetToken=fixture-A":@"AssetToken=fixture-B"};
}
static NSDictionary *SlowAuthorization(id object,SEL selector,NSURL *URL,BOOL (^cancelled)(void),NSError **error) {
    for(NSUInteger tick=0;tick<200;tick++) {
        if(cancelled()) {
            atomic_store(&authorizationCancelled,true);
            if(error)*error=[NSError errorWithDomain:NSURLErrorDomain code:NSURLErrorCancelled userInfo:nil];return nil;
        }
        usleep(10000);
    }
    return nil;
}
@interface FastDownload : VIMDownload
@end
@implementation FastDownload
- (NSTimeInterval)retryDelayForError:(NSError *)error consecutiveRetry:(NSUInteger)retry {return .01;}
@end

static NSUInteger failures;
static NSString *root;
static NSString *eventsPath;
static void Require(BOOL value,NSString *message) {
    if(value)printf("PASS: %s\n",message.UTF8String);
    else {fprintf(stderr,"FAIL: %s\n",message.UTF8String);failures++;}
}
static NSArray *Events(NSString *path) {
    NSMutableArray *result=[NSMutableArray new];
    for(NSString *line in [[NSString stringWithContentsOfFile:eventsPath encoding:NSUTF8StringEncoding error:NULL] componentsSeparatedByString:@"\n"]) {
        NSDictionary *entry=[NSJSONSerialization JSONObjectWithData:[line dataUsingEncoding:NSUTF8StringEncoding] options:0 error:NULL];
        if([entry[@"path"] isEqual:path])[result addObject:entry];
    }
    return result;
}
static BOOL Run(VIMDownload *download,BOOL wantSuccess,BOOL wantPause) {
    __block BOOL done=NO,correct=NO;__block NSUInteger completions=0;
    [download startWithCompletion:^(NSURL *file,NSError *error,BOOL paused){
        completions++;correct=wantSuccess?(file&&!error&&!paused):wantPause?(paused&&!file&&!error):(error&&!file&&!paused);
        done=YES;
    }];
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:10];
    while(!done&&deadline.timeIntervalSinceNow>0)[NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.01]];
    [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.1]];
    if(!done)[download pause];
    return correct&&completions==1;
}
static VIMDownload *Download(NSString *test,BOOL fast) {
    NSURL *source=[NSURL URLWithString:[NSString stringWithFormat:@"https://swcdn.apple.com/content/downloads/vim-test/%@/Payload.pkg",test]];
    NSURL *directory=[NSURL fileURLWithPath:[root stringByAppendingPathComponent:test]];
    return [[(fast?FastDownload.class:VIMDownload.class) alloc] initWithSourceURL:source directory:directory error:NULL];
}
static void Seed(VIMDownload *download) {
    NSFileManager *fm=NSFileManager.defaultManager;
    [fm createDirectoryAtURL:download.directory withIntermediateDirectories:YES attributes:nil error:NULL];
    NSMutableData *prefix=[NSMutableData dataWithLength:65536];
    for(NSUInteger index=0;index<prefix.length;index++)((unsigned char *)prefix.mutableBytes)[index]=index%251;
    [prefix writeToURL:[download.directory URLByAppendingPathComponent:@"Payload.partial"] atomically:YES];
    [@{@"FormatVersion":@1,@"SourceURL":download.sourceURL.absoluteString,@"Length":@524288,@"ETag":@"\"fixture-v1\""}
        writeToURL:[download.directory URLByAppendingPathComponent:@"Download.plist"] atomically:YES];
}
int main(int argc,char **argv) { @autoreleasepool {
    if(argc!=4)return 64;
    setvbuf(stdout,NULL,_IOLBF,0);serverURL=@(argv[1]);root=@(argv[2]);eventsPath=@(argv[3]);
    method_setImplementation(class_getClassMethod(VIMInstallerCatalog.class,@selector(fileForURL:)),(IMP)FixtureRecord);
    [NSUserDefaults.standardUserDefaults setVolatileDomain:@{@"VIMDebugMode":@YES,@"VIMDownloadServer":@(argv[1])} forName:NSArgumentDomain];
    VIMDownload *interrupted=Download(@"disconnect",YES);
    Require(Run(interrupted,YES,NO),@"a dropped transfer automatically resumes and passes its digest");
    NSArray *requests=Events(interrupted.sourceURL.path);
    Require(requests.count==2&&[requests.lastObject[@"range"] isEqual:@"bytes=65536-"]&&
        [requests.lastObject[@"ifRange"] isEqual:@"\"fixture-v1\""],@"automatic recovery uses the exact persisted Range and validator");
    Require(Run(Download(@"busy",YES),YES,NO),@"temporary HTTP failures recover without user action");
    Require(Events(@"/content/downloads/vim-test/busy/Payload.pkg").count==3,@"HTTP recovery stops retrying after success");
    VIMDownload *missing=Download(@"missing",YES);
    Require(Run(missing,NO,NO)&&Events(missing.sourceURL.path).count==1,@"404 remains a terminal error");
    VIMDownload *unavailable=Download(@"unavailable",YES);
    Require(Run(unavailable,NO,NO)&&Events(unavailable.sourceURL.path).count==7,@"six consecutive retries exhaust the recovery budget");
    VIMDownload *progressLoop=Download(@"progress-loop",YES);
    BOOL stopped=Run(progressLoop,NO,NO);
    NSDictionary *partial=[NSFileManager.defaultManager attributesOfItemAtPath:[progressLoop.directory URLByAppendingPathComponent:@"Payload.partial"].path error:NULL];
    Require(stopped&&Events(progressLoop.sourceURL.path).count==21&&[partial[NSFileSize] longLongValue]==21LL*1024*1024,
        @"progress cannot reset the twenty-retry total budget");
    VIMDownload *badRange=Download(@"bad-range",YES);Seed(badRange);
    Require(Run(badRange,NO,NO)&&Events(badRange.sourceURL.path).count==1,@"malformed partial responses do not retry or append");
    VIMDownload *badHash=Download(@"bad-hash",YES);
    Require(Run(badHash,NO,NO)&&Events(badHash.sourceURL.path).count==1&&
        ![NSFileManager.defaultManager fileExistsAtPath:[badHash.directory URLByAppendingPathComponent:@"Payload.pkg"].path],@"digest failure cannot publish or retry a corrupt payload");
    VIMDownload *changed=Download(@"changed",YES);Seed(changed);
    Require(Run(changed,YES,NO),@"a changed validator and HTTP 200 replace the partial file safely");
    VIMDownload *limited=Download(@"rate-limit",NO);CFAbsoluteTime start=CFAbsoluteTimeGetCurrent();
    Require(Run(limited,YES,NO)&&CFAbsoluteTimeGetCurrent()-start>=1,@"Retry-After delays the next request");
    VIMDownload *longWait=Download(@"long-wait",NO);
    Require(Run(longWait,NO,NO)&&Events(longWait.sourceURL.path).count==1,@"a server delay beyond the automatic recovery window stops without an early retry");
    VIMDownload *waiting=Download(@"pause-wait",NO);__weak VIMDownload *weakWaiting=waiting;
    waiting.stateChanged=^(VIMDownloadState state,NSTimeInterval delay){
        if(state==VIMDownloadStateWaitingToRetry)[weakWaiting pause];
    };
    Require(Run(waiting,NO,YES),@"pause interrupts backoff and completes once");
    [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:1.4]];
    Require(Events(waiting.sourceURL.path).count==1,@"a paused retry timer cannot start another request");
    Method mapping=class_getClassMethod(VIMInstallerCatalog.class,@selector(downloadURLForAppleURL:));
    Method authorization=class_getClassMethod(VIMInstallerSource.class,@selector(headersForURL:cancelled:error:));
    IMP oldMapping=method_setImplementation(mapping,(IMP)OriginalMapping);
    IMP oldAuthorization=method_setImplementation(authorization,(IMP)SlowAuthorization);
    VIMDownload *authorizing=Download(@"pause-authorization",YES);__weak VIMDownload *weakAuthorizing=authorizing;
    authorizing.stateChanged=^(VIMDownloadState state,NSTimeInterval delay){
        if(state==VIMDownloadStateAuthorizing)
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC/20),dispatch_get_main_queue(),^{[weakAuthorizing pause];});
    };
    Require(Run(authorizing,NO,YES)&&atomic_load(&authorizationCancelled),@"pause cancels pending authorization without waiting for a download task");
    method_setImplementation(mapping,oldMapping);method_setImplementation(authorization,oldAuthorization);
    Require(Events(authorizing.sourceURL.path).count==0,@"late authorization cannot issue a request after pause");
    __block BOOL cancelled=NO;dispatch_semaphore_t authorized=dispatch_semaphore_create(0);
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{
        NSError *error=nil;
        NSDictionary *headers=[VIMInstallerSource headersForURL:[NSURL URLWithString:@"http://oscdn.apple.com/content/downloads/test/InstallESD.dmg"] cancelled:^BOOL{return YES;} error:&error];
        cancelled=!headers&&[error.domain isEqual:NSURLErrorDomain]&&error.code==NSURLErrorCancelled;
        dispatch_semaphore_signal(authorized);
    });
    dispatch_semaphore_wait(authorized,DISPATCH_TIME_FOREVER);
    Require(cancelled,@"an already cancelled recovery authorization does not contact Apple");
    NSHTTPURLResponse *response=[[NSHTTPURLResponse alloc] initWithURL:[NSURL URLWithString:@"https://osrecovery.apple.com/"] statusCode:503 HTTPVersion:@"HTTP/1.1" headerFields:@{@"Retry-After":@"Wed, 21 Oct 2015 07:28:00 GMT"}];
    NSDictionary *info=[VIMInstallerSource errorInfoForHTTPResponse:response message:@"fixture"];
    Require([info[VIMInstallerHTTPStatusKey] isEqual:@503]&&[info[VIMInstallerRetryAfterKey] timeIntervalSince1970]==1445412480.,@"authorization HTTP metadata preserves status and HTTP-date Retry-After");
    NSURL *original=[NSURL URLWithString:@"http://oscdn.apple.com/content/downloads/test/InstallESD.dmg"];
    NSURL *alternate=[VIMInstallerSource transportURLForSourceURL:original retryAttempt:1];
    Require([alternate.host isEqual:@"oscdn.apple.com.edgesuite.net"]&&[alternate.path isEqual:original.path]&&
        [[VIMInstallerSource transportURLForSourceURL:original retryAttempt:2] isEqual:original]&&
        ![VIMInstallerSource isApprovedURL:alternate],@"Mavericks fallback changes transport only and cannot replace a catalog source");
    VIMDownload *locked=Download(@"lock-wait",NO);__weak VIMDownload *weakLocked=locked;__block BOOL excluded=NO;
    locked.stateChanged=^(VIMDownloadState state,NSTimeInterval delay){
        if(state!=VIMDownloadStateWaitingToRetry)return;
        VIMDownload *other=[[VIMDownload alloc] initWithSourceURL:weakLocked.sourceURL directory:weakLocked.directory error:NULL];
        [other startWithCompletion:^(NSURL *file,NSError *error,BOOL paused){excluded=error&&!file&&!paused;[weakLocked pause];}];
    };
    Require(Run(locked,NO,YES)&&excluded,@"the download retains exclusive ownership while waiting to retry");
    VIMDownload *noValidator=Download(@"no-validator",YES);Seed(noValidator);
    NSURL *record=[noValidator.directory URLByAppendingPathComponent:@"Download.plist"];
    NSMutableDictionary *metadata=[[NSDictionary dictionaryWithContentsOfURL:record] mutableCopy];
    [metadata removeObjectForKey:@"ETag"];[metadata writeToURL:record atomically:YES];
    Require(Run(noValidator,YES,NO)&&[Events(noValidator.sourceURL.path).firstObject[@"range"] isEqual:NSNull.null],@"a partial file without a validator restarts instead of appending");
    VIMDownload *emptyETag=Download(@"empty-etag",YES);Seed(emptyETag);
    record=[emptyETag.directory URLByAppendingPathComponent:@"Download.plist"];
    metadata=[[NSDictionary dictionaryWithContentsOfURL:record] mutableCopy];
    metadata[@"ETag"]=@"";metadata[@"LastModified"]=@"Wed, 21 Oct 2015 07:28:00 GMT";
    [metadata writeToURL:record atomically:YES];
    BOOL resumed=Run(emptyETag,YES,NO);
    NSDictionary *resumedRequest=Events(emptyETag.sourceURL.path).firstObject;
    Require(resumed&&[resumedRequest[@"range"] isEqual:@"bytes=65536-"]&&
        [resumedRequest[@"ifRange"] isEqual:metadata[@"LastModified"]],@"an empty ETag falls back to Last-Modified for persisted resume");
    VIMDownload *pause=Download(@"pause-active",YES);__weak VIMDownload *weakPause=pause;
    pause.progress=^(int64_t received,int64_t total){if(received>=65536)[weakPause pause];};
    Require(Run(pause,NO,YES),@"explicit pause returns once and preserves bytes");
    pause.progress=nil;
    Require(Run(pause,YES,NO),@"the same download instance can resume after an explicit pause");
    VIMDownload *cached=Download(@"complete",YES);
    BOOL initialComplete=Run(cached,YES,NO);
    Method verification=class_getClassMethod(VIMInstallerSource.class,@selector(verifyFile:source:error:));
    originalVerification=method_setImplementation(verification,(IMP)SlowVerification);
    __weak VIMDownload *weakCached=cached;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC/50),dispatch_get_main_queue(),^{[weakCached pause];});
    Require(initialComplete&&Run(cached,NO,YES),@"pause during cached-file verification cannot advance to preparation");
    VIMDownload *pauseBadHash=Download(@"pause-bad-hash",YES);__weak VIMDownload *weakBadHash=pauseBadHash;
    pauseBadHash.stateChanged=^(VIMDownloadState state,NSTimeInterval delay){
        if(state==VIMDownloadStateVerifying)[weakBadHash pause];
    };
    Require(Run(pauseBadHash,NO,YES)&&![NSFileManager.defaultManager fileExistsAtPath:[pauseBadHash.directory URLByAppendingPathComponent:@"Payload.pkg"].path],@"pause during failed fresh-file verification returns paused without publishing");
    method_setImplementation(verification,originalVerification);
    Method transport=class_getClassMethod(VIMInstallerSource.class,@selector(transportURLForSourceURL:retryAttempt:));
    oldMapping=method_setImplementation(mapping,(IMP)OriginalMapping);
    oldAuthorization=method_setImplementation(authorization,(IMP)FixtureAuthorization);
    IMP oldTransport=method_setImplementation(transport,(IMP)FixtureTransport);
    for(NSString *test in @[@"auth-refresh",@"auth-denied",@"auth-control",@"redirect-auth"]) {
        authorizationMode=test;atomic_store(&authorizationCalls,0);
        NSURL *source=[NSURL URLWithString:[NSString stringWithFormat:@"http://oscdn.apple.com/content/downloads/vim-test/%@/InstallESD.dmg",test]];
        NSURL *directory=[NSURL fileURLWithPath:[root stringByAppendingPathComponent:test]];
        BOOL fast=![test isEqual:@"auth-control"];
        VIMDownload *transfer=[[(fast?FastDownload.class:VIMDownload.class) alloc] initWithSourceURL:source directory:directory error:NULL];
        CFAbsoluteTime began=CFAbsoluteTimeGetCurrent();
        BOOL expected=[@[@"auth-refresh",@"auth-control"] containsObject:test];
        BOOL outcome=Run(transfer,expected,NO);
        NSArray *requests=Events(source.path);
        if([test isEqual:@"auth-refresh"]) {
            NSDictionary *metadata=[NSDictionary dictionaryWithContentsOfURL:[directory URLByAppendingPathComponent:@"Download.plist"]];
            Require(outcome&&atomic_load(&authorizationCalls)==2&&requests.count==2&&
                [requests.firstObject[@"fixtureToken"] isEqual:@"A"]&&[requests.lastObject[@"fixtureToken"] isEqual:@"B"]&&
                [requests.lastObject[@"host"] hasPrefix:@"localhost:"]&&[metadata[@"SourceURL"] isEqual:source.absoluteString],
                @"403 acquires a fresh token for the alternate transport and keeps the original source/digest");
        } else if([test isEqual:@"auth-denied"])
            Require(outcome&&atomic_load(&authorizationCalls)==2&&requests.count==2,@"a repeated authenticated 403 is terminal");
        else if([test isEqual:@"auth-control"])
            Require(outcome&&atomic_load(&authorizationCalls)==2&&requests.count==1&&CFAbsoluteTimeGetCurrent()-began>=1,
                @"authorization service failure retries with fresh credentials and honors Retry-After");
        else Require(outcome&&Events(@"/token-sink/InstallESD.dmg").count==0,@"authenticated redirects cannot send a recovery token to loopback");
    }
    method_setImplementation(mapping,oldMapping);method_setImplementation(authorization,oldAuthorization);method_setImplementation(transport,oldTransport);
    printf("Recovery suite: %lu failures\n",(unsigned long)failures);return failures?1:0;
}}
