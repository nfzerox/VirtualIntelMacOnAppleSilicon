#import "VIMDownload.h"
#import "VIMInstallerCatalog.h"
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
static NSError *DownloadError(NSString *message) {
    return [NSError errorWithDomain:@"VirtualIntelMac.Download" code:1 userInfo:@{NSLocalizedDescriptionKey:message}];
}
@interface VIMDownload () <NSURLSessionDataDelegate>
@property(nonatomic) NSURL *directory;
@property(nonatomic) NSURL *sourceURL;
@property(nonatomic) NSURLSession *session;
@property(nonatomic) NSURLSessionDataTask *task;
@property(nonatomic) NSOperationQueue *queue;
@property(nonatomic) int file;
@property(nonatomic) int lockFile;
@property(nonatomic) int64_t received;
@property(nonatomic) int64_t total;
@property(nonatomic) NSMutableDictionary *metadata;
@property(nonatomic) NSError *failure;
@property(nonatomic) BOOL pausing;
@property(nonatomic) BOOL accepted;
@property(nonatomic) BOOL active;
@property(nonatomic) CFAbsoluteTime lastProgress;
@property(nonatomic,copy) void (^completion)(NSURL *,NSError *,BOOL);
@end
@implementation VIMDownload
- (instancetype)initWithSourceURL:(NSURL *)URL directory:(NSURL *)directory error:(NSError **)error {
    if(![URL.scheme isEqual:@"https"]||![URL.host isEqual:@"swcdn.apple.com"]||URL.user||URL.password||!directory.isFileURL) {
        if(error)*error=DownloadError(@"Invalid installer download location.");return nil;
    }
    if((self=[super init])) {
        _sourceURL=URL;_directory=directory.URLByResolvingSymlinksInPath;_file=-1;_lockFile=-1;_total=-1;
        _queue=[NSOperationQueue new];_queue.maxConcurrentOperationCount=1;
        _queue.qualityOfService=NSQualityOfServiceUtility;
    }
    return self;
}
- (NSURL *)metadataURL { return [self.directory URLByAppendingPathComponent:@"Download.plist"]; }
- (NSURL *)partialURL { return [self.directory URLByAppendingPathComponent:@"Payload.partial"]; }
- (NSURL *)finishedURL { return [self.directory URLByAppendingPathComponent:@"Payload.pkg"]; }
- (BOOL)saveMetadata:(NSError **)error {
    NSData *data=[NSPropertyListSerialization dataWithPropertyList:self.metadata format:NSPropertyListXMLFormat_v1_0 options:0 error:error];
    return data && [data writeToURL:self.metadataURL options:NSDataWritingAtomic error:error];
}
- (void)startWithCompletion:(void (^)(NSURL *,NSError *,BOOL))completion {
    NSAssert(NSThread.isMainThread,@"Download lifecycle is main-thread owned");
    if(self.active) { completion(nil,DownloadError(@"This download is already running."),NO);return; }
    self.active=YES;self.completion=completion;self.pausing=NO;self.failure=nil;self.accepted=NO;
    [self.queue addOperationWithBlock:^{[self beginTransfer];}];
}
- (void)beginTransfer {
    NSError *error=nil;NSFileManager *fm=NSFileManager.defaultManager;
    if(![fm createDirectoryAtURL:self.directory withIntermediateDirectories:YES attributes:nil error:&error]) { [self finish:nil error:error];return; }
    self.lockFile=open([[self.directory URLByAppendingPathComponent:@"Download.lock"] fileSystemRepresentation],O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);
    if(self.lockFile<0 || flock(self.lockFile,LOCK_EX|LOCK_NB)) { [self finish:nil error:DownloadError(@"Another process is using this download.")];return; }
    NSDictionary *stored=[NSDictionary dictionaryWithContentsOfURL:self.metadataURL];
    if(stored && (![stored[@"FormatVersion"] isEqual:@1] || ![stored[@"SourceURL"] isEqual:self.sourceURL.absoluteString])) {
        [self finish:nil error:DownloadError(@"This download folder belongs to another file.")];return;
    }
    self.metadata=stored?[stored mutableCopy]:[@{@"FormatVersion":@1,@"SourceURL":self.sourceURL.absoluteString} mutableCopy];
    for(NSString *key in @[@"Length",@"Complete"])if(self.metadata[key] && ![self.metadata[key] isKindOfClass:NSNumber.class]) {
        [self finish:nil error:DownloadError(@"Invalid download state.")];return;
    }
    // Recover an interruption between the final rename and completion journal.
    NSDictionary *finished=[fm attributesOfItemAtPath:self.finishedURL.path error:NULL];
    if(![self.metadata[@"Complete"] boolValue] && [finished[NSFileType] isEqual:NSFileTypeRegular] &&
        [finished[NSFileSize] longLongValue]>0 && [finished[NSFileSize] isEqual:self.metadata[@"Length"]]) {
        self.metadata[@"Complete"]=@YES;
        if(![self saveMetadata:&error]) { [self finish:nil error:error];return; }
    }
    if([self.metadata[@"Complete"] boolValue]) {
        NSDictionary *attributes=[fm attributesOfItemAtPath:self.finishedURL.path error:NULL];
        if([attributes[NSFileType] isEqual:NSFileTypeRegular] && [attributes[NSFileSize] longLongValue]>0 &&
            [attributes[NSFileSize] isEqual:self.metadata[@"Length"]]) { [self finish:self.finishedURL error:nil];return; }
        if(!finished) {
            // Preparation retired the completed payload. A crash between its
            // unlink and metadata removal must not make this URL unresumable.
            [self.metadata removeObjectsForKeys:@[@"Complete",@"Length",@"ETag",@"LastModified"]];
            if(![self saveMetadata:&error]){[self finish:nil error:error];return;}
        } else { [self finish:nil error:DownloadError(@"The cached installer is incomplete. Remove this download through the app before retrying.")];return; }
    }
    self.file=open(self.partialURL.fileSystemRepresentation,O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);
    struct stat st;
    if(self.file<0||flock(self.file,LOCK_EX|LOCK_NB)||fstat(self.file,&st)||!S_ISREG(st.st_mode)) {
        [self finish:nil error:DownloadError(@"Cannot open this download, or another process is using it.")];return;
    }
    self.received=st.st_size;
    if(self.received>0 && self.received==[self.metadata[@"Length"] longLongValue]) {
        self.accepted=YES;self.total=self.received;
        [self completeTransferWithError:nil];return;
    }
    NSString *etag=self.metadata[@"ETag"],*modified=self.metadata[@"LastModified"];
    NSString *validator=[etag isKindOfClass:NSString.class] && etag.length && ![etag hasPrefix:@"W/"]?etag:
        ([modified isKindOfClass:NSString.class]?modified:nil);
    if(self.received && !validator.length) {
        if(ftruncate(self.file,0)) { [self finish:nil error:DownloadError(@"Could not restart the partial download.")];return; }
        self.received=0;
    }
    if(![self saveMetadata:&error]) { [self finish:nil error:error];return; }
    NSURL *actual=[VIMInstallerCatalog downloadURLForAppleURL:self.sourceURL];
    NSMutableURLRequest *request=[NSMutableURLRequest requestWithURL:actual cachePolicy:NSURLRequestReloadIgnoringLocalCacheData timeoutInterval:30];
    [request setValue:@"identity" forHTTPHeaderField:@"Accept-Encoding"];
    if(self.received) {
        [request setValue:[NSString stringWithFormat:@"bytes=%lld-",self.received] forHTTPHeaderField:@"Range"];
        [request setValue:validator forHTTPHeaderField:@"If-Range"];
    }
    NSURLSessionConfiguration *configuration=NSURLSessionConfiguration.ephemeralSessionConfiguration;
    configuration.URLCache=nil;configuration.timeoutIntervalForRequest=30;
    configuration.timeoutIntervalForResource=7*24*3600;
    self.session=[NSURLSession sessionWithConfiguration:configuration delegate:self delegateQueue:self.queue];
    self.task=[self.session dataTaskWithRequest:request];[self.task resume];
}
- (void)URLSession:(NSURLSession *)session dataTask:(NSURLSessionDataTask *)task didReceiveResponse:(NSURLResponse *)response completionHandler:(void (^)(NSURLSessionResponseDisposition))completionHandler {
    NSHTTPURLResponse *http=[response isKindOfClass:NSHTTPURLResponse.class]?(id)response:nil;
    int64_t offset=self.received;
    BOOL valid=http && (http.statusCode==200||http.statusCode==206);
    self.total=response.expectedContentLength;
    if(valid && http.statusCode==206) {
        NSString *range=[http valueForHTTPHeaderField:@"Content-Range"];
        long long first=-1,last=-1,total=-1;char extra;
        valid=range && sscanf(range.UTF8String,"bytes %lld-%lld/%lld%c",&first,&last,&total,&extra)==3 &&
            first==offset && last>=first && total>last && total<=100LL*1024*1024*1024 && last==total-1;
        self.total=total;
        NSString *previous=self.metadata[@"ETag"],*current=[http valueForHTTPHeaderField:@"ETag"];
        if([previous isKindOfClass:NSString.class] && ![previous hasPrefix:@"W/"] && current && ![previous isEqual:current])valid=NO;
    } else if(valid) {
        // If-Range mismatch or ignored Range: replace, never append a full body.
        if(offset && ftruncate(self.file,0))valid=NO;
        self.received=0;
    }
    NSString *encoding=[http valueForHTTPHeaderField:@"Content-Encoding"];
    if(encoding.length && ![encoding.lowercaseString isEqual:@"identity"])valid=NO;
    if(self.total>100LL*1024*1024*1024)valid=NO;
    if(!valid) {
        self.failure=DownloadError([NSString stringWithFormat:@"The server returned an invalid download response (HTTP %ld).",http.statusCode]);
        completionHandler(NSURLSessionResponseCancel);return;
    }
    if(lseek(self.file,self.received,SEEK_SET)<0) {
        self.failure=DownloadError(@"Could not position the partial download.");completionHandler(NSURLSessionResponseCancel);return;
    }
    NSString *etag=[http valueForHTTPHeaderField:@"ETag"],*modified=[http valueForHTTPHeaderField:@"Last-Modified"];
    if(etag)self.metadata[@"ETag"]=etag;else [self.metadata removeObjectForKey:@"ETag"];
    if(modified)self.metadata[@"LastModified"]=modified;else [self.metadata removeObjectForKey:@"LastModified"];
    self.metadata[@"Length"]=@(self.total);
    NSError *error=nil;
    if(![self saveMetadata:&error]) { self.failure=error;completionHandler(NSURLSessionResponseCancel);return; }
    self.accepted=YES;completionHandler(NSURLSessionResponseAllow);
}
- (void)URLSession:(NSURLSession *)session dataTask:(NSURLSessionDataTask *)task didReceiveData:(NSData *)data {
    if(!self.accepted||self.failure)return;
    if(self.received>100LL*1024*1024*1024-(int64_t)data.length ||
        (self.total>=0 && (int64_t)data.length>self.total-self.received)) {
        self.failure=DownloadError(@"The download exceeded its expected length.");[task cancel];return;
    }
    const uint8_t *bytes=data.bytes;size_t remaining=data.length;
    while(remaining) {
        ssize_t n=write(self.file,bytes,remaining);
        if(n<0&&errno==EINTR)continue;
        if(n<=0) { self.failure=DownloadError(@"Could not save the download. Check available disk space.");[task cancel];return; }
        bytes+=n;remaining-=n;self.received+=n;
    }
    CFAbsoluteTime now=CFAbsoluteTimeGetCurrent();
    if(now-self.lastProgress>=0.2) {
        self.lastProgress=now;int64_t received=self.received,total=self.total;
        dispatch_async(dispatch_get_main_queue(), ^{if(self.progress)self.progress(received,total);});
    }
}
- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task willPerformHTTPRedirection:(NSHTTPURLResponse *)response newRequest:(NSURLRequest *)request completionHandler:(void (^)(NSURLRequest *))completionHandler {
    NSURL *URL=request.URL,*original=task.originalRequest.URL;
    BOOL allowed=([URL.scheme isEqual:@"https"]&&[URL.host isEqual:@"swcdn.apple.com"]&&!URL.user&&!URL.password) ||
        ([original.scheme isEqual:@"http"]&&[URL.scheme isEqual:original.scheme]&&[URL.host isEqual:original.host]&&[URL.port isEqual:original.port]);
    if(!allowed)self.failure=DownloadError(@"The installer server redirected to an unapproved location.");
    completionHandler(allowed?request:nil);
}
- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task didCompleteWithError:(NSError *)error {
    [self completeTransferWithError:error];
}
- (void)completeTransferWithError:(NSError *)error {
    error=self.failure?:error;
    if(self.pausing) { [self finish:nil error:nil];return; }
    if(!error && (!self.accepted || !self.received || (self.total>=0&&self.received!=self.total)))error=DownloadError(@"The installer download did not finish completely.");
    if(!error && fsync(self.file))error=DownloadError(@"Could not flush the completed download to disk.");
    if(!error) {
        if(![NSFileManager.defaultManager moveItemAtURL:self.partialURL toURL:self.finishedURL error:&error]) { [self finish:nil error:error];return; }
        self.metadata[@"Complete"]=@YES;self.metadata[@"Length"]=@(self.received);
        [self saveMetadata:&error];
    }
    [self finish:error?nil:self.finishedURL error:error];
}
- (void)pause {
    NSAssert(NSThread.isMainThread,@"Download lifecycle is main-thread owned");
    [self.queue addOperationWithBlock:^{if(self.task){self.pausing=YES;[self.task cancel];}}];
}
- (void)finish:(NSURL *)file error:(NSError *)error {
    if(self.file>=0) { fsync(self.file);close(self.file);self.file=-1; }
    if(self.lockFile>=0) { close(self.lockFile);self.lockFile=-1; }
    [self.session finishTasksAndInvalidate];self.session=nil;self.task=nil;
    BOOL paused=self.pausing;
    dispatch_async(dispatch_get_main_queue(), ^{
        self.active=NO;void (^completion)(NSURL *,NSError *,BOOL)=self.completion;self.completion=nil;
        if(completion)completion(file,error,paused);
    });
}
@end
