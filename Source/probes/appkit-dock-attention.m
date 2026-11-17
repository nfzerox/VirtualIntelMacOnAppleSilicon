// Finite measurement workload: ask the real guest Dock to bounce this icon.
// No custom animation/window, input posting, system hooks or guest preferences.
// Open the built .app with `open -g -n ... --args NEW_ABSOLUTE_LOG_PATH`.
// On Leopard invoke its executable with --launch ABSOLUTE_APP_PATH NEW_LOG.
#import <Cocoa/Cocoa.h>
#import <ApplicationServices/ApplicationServices.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

@interface VIMDockAttention : NSObject <NSApplicationDelegate> {
    NSFileHandle *_log;
    NSInteger _request;
}
- (id)initWithLog:(NSFileHandle *)log;
@end
@implementation VIMDockAttention
- (id)initWithLog:(NSFileHandle *)log {
    if((self=[super init]))_log=[log retain];
    return self;
}
- (void)record:(NSString *)text {
    NSString *line=[NSString stringWithFormat:@"%.6f %@\n",[[NSDate date] timeIntervalSince1970],text];
    [_log writeData:[line dataUsingEncoding:NSUTF8StringEncoding]];
}
- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    (void)notification;
    if([NSApp isActive]) {
        [self record:@"UNQUALIFIED: launched active; use open -g"];
        exit(77);
    }
    _request=[NSApp requestUserAttention:NSCriticalRequest];
    [self record:[NSString stringWithFormat:@"BEGIN real Dock attention request=%ld active=%d",
        (long)_request,[NSApp isActive]]];
    [NSTimer scheduledTimerWithTimeInterval:12 target:self selector:@selector(finish:)
        userInfo:nil repeats:NO];
}
- (void)finish:(NSTimer *)timer {
    (void)timer;
    [NSApp cancelUserAttentionRequest:_request];
    [self record:@"END real Dock attention"];
    [_log synchronizeFile];
    [NSApp terminate:nil];
}
@end

int main(int argc,const char **argv) {
    // Leopard's `open` has no --args. Use the same public Launch Services
    // background/new-instance request, without changing launchd's environment.
    if(argc==4&&!strcmp(argv[1],"--launch")) {
        if(argv[2][0]!='/'||argv[3][0]!='/')return 64;
        FSRef app;
        OSStatus status=FSPathMakeRef((const UInt8 *)argv[2],&app,NULL);
        if(status!=noErr)return 66;
        CFStringRef argument=CFStringCreateWithCString(NULL,argv[3],kCFStringEncodingUTF8);
        CFArrayRef arguments=CFArrayCreate(NULL,(const void **)&argument,1,&kCFTypeArrayCallBacks);
        LSApplicationParameters parameters={0};
        parameters.application=&app;
        parameters.flags=kLSLaunchDontSwitch|kLSLaunchNewInstance;
        parameters.argv=arguments;
        status=LSOpenApplication(&parameters,NULL);
        CFRelease(arguments);CFRelease(argument);
        fprintf(stderr,"LSOpenApplication status=%d\n",(int)status);
        return status==noErr?0:69;
    }
    if(argc!=2||argv[1][0]!='/')return 64;
    int fd=open(argv[1],O_WRONLY|O_CREAT|O_EXCL,0600);
    if(fd<0)return 73;
    alarm(25); // Diagnostic bound, unrelated to installation/setup deadlines.
    NSAutoreleasePool *pool=[[NSAutoreleasePool alloc] init];
    {
        [NSApplication sharedApplication];
        // A normal APPL bundle is regular on 10.5 too; activationPolicy was
        // introduced in 10.6. Keep this same Dock workload usable on Leopard.
        NSFileHandle *log=[[NSFileHandle alloc] initWithFileDescriptor:fd closeOnDealloc:YES];
        VIMDockAttention *delegate=[[VIMDockAttention alloc] initWithLog:log];
        [NSApp setDelegate:delegate];
        [NSApp run];
        [delegate release];[log release];
    }
    [pool drain];
    return 0;
}
