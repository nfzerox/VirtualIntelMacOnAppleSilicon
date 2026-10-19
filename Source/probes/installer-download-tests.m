#import <Foundation/Foundation.h>
#import "VIMDownload.h"
int main(int argc,char **argv) { @autoreleasepool {
    if(argc!=3)return 64;
    [NSUserDefaults.standardUserDefaults setVolatileDomain:@{@"VIMDebugMode":@YES,@"VIMDownloadServer":@(argv[1])} forName:NSArgumentDomain];
    NSURL *source=[NSURL URLWithString:@"https://swcdn.apple.com/content/downloads/vim-test/Payload.pkg"];
    NSURL *directory=[NSURL fileURLWithPath:@(argv[2])];
    __block BOOL finished=NO,requestedPause=NO;__block int result=1;
    __block VIMDownload *download=[[VIMDownload alloc] initWithSourceURL:source directory:directory error:NULL];
    __weak VIMDownload *firstDownload=download;
    download.progress=^(int64_t received,int64_t total){if(received>=131072 && !requestedPause){requestedPause=YES;[firstDownload pause];}};
    [download startWithCompletion:^(NSURL *file,NSError *error,BOOL paused){
        if(error || !paused) { NSLog(@"Pause failed: %@",error);finished=YES;return; }
        // Discard the transfer instance: resume relies on persisted bytes/state,
        // not an in-memory NSURLSession resume blob.
        download=[[VIMDownload alloc] initWithSourceURL:source directory:directory error:NULL];
        [download startWithCompletion:^(NSURL *file,NSError *error,BOOL paused){
            if(error || !file || paused) { NSLog(@"Resume failed: %@",error);finished=YES;return; }
            NSData *data=[NSData dataWithContentsOfURL:file];
            BOOL valid=data.length==8*1024*1024;
            const uint8_t *bytes=data.bytes;for(NSUInteger i=0;valid && i<data.length;i++)if(bytes[i])valid=NO;
            if(!valid) { NSLog(@"Downloaded bytes mismatch");finished=YES;return; }
            download=[[VIMDownload alloc] initWithSourceURL:source directory:directory error:NULL];
            [download startWithCompletion:^(NSURL *cached,NSError *error,BOOL paused){
                result=cached && !error && !paused?0:1;finished=YES;
            }];
        }];
    }];
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:30];
    while(!finished && deadline.timeIntervalSinceNow>0)[NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
    if(!finished)[download pause];
    puts(result==0?"PASS: pause, persisted resume, exact bytes, completed cache reuse":"FAIL: persistent download");return result;
} }
