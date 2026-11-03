// Finite acquisition-policy check. No payload downloads or secret output.
#import <Foundation/Foundation.h>
#import "VIMInstallerCatalog.h"
#import "VIMInstallerSource.h"
int main(int argc,const char **argv) {@autoreleasepool {
    if(argc<2||argc>3)return 64;
    NSError *error=nil;
    VIMInstallerCatalog *catalog=[VIMInstallerCatalog catalogWithData:[NSData dataWithContentsOfFile:@(argv[1])] error:&error];
    if(!catalog){fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);return 1;}
    for(NSString *rejected in @[@"http://swcdn.apple.com/content/downloads/test.pkg",@"http://oscdn.apple.com/other.dmg",@"https://example.com/InstallMacOSX.dmg",@"https://oscdn.apple.com/content/downloads/33/62/031-10295/gho4r94w66f5v4ujm0sz7k1m0hua68i6oo/OSInstaller/InstallESD.dmg"])
        if([VIMInstallerSource isApprovedURL:[NSURL URLWithString:rejected]]){fprintf(stderr,"FAIL: unexpected source accepted\n");return 1;}
    printf("catalog revision=%lu count=%lu\n",(unsigned long)catalog.revision,(unsigned long)catalog.supportedInstallers.count);
    for(NSDictionary *entry in catalog.supportedInstallers)printf("%s %s\n",[entry[@"Guest"] UTF8String],[entry[@"Assembly"] UTF8String]);
    if(argc==3&&strcmp(argv[2],"--authorize-mavericks"))return 64;
    if(argc==3){
        __block BOOL success=NO;dispatch_semaphore_t done=dispatch_semaphore_create(0);
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{
            NSError *failure=nil;
            for(NSDictionary *entry in catalog.supportedInstallers)if([entry[@"Guest"] isEqual:@"mavericks"]){
                NSURL *URL=[NSURL URLWithString:entry[@"Files"][0][@"URL"]];
                success=[VIMInstallerSource headersForURL:URL error:&failure]!=nil;
            }
            if(failure)fprintf(stderr,"%s\n",failure.localizedDescription.UTF8String);
            dispatch_semaphore_signal(done);
        });dispatch_semaphore_wait(done,DISPATCH_TIME_FOREVER);
        printf("recovery-authorization=%s (no tokens printed)\n",success?"PASS":"FAIL");return success?0:1;
    }
    return 0;
}}
