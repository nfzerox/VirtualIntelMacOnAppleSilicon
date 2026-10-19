// A real, ad-hoc-signed GUI client for the guest's normal microphone consent.
// No private entitlements, TCC database access, audio files or saved recording.
#import <AppKit/AppKit.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreAudio/CoreAudio.h>
#include <unistd.h>
extern int csops(pid_t,unsigned int,void *,size_t);

@interface MicrophoneProbe : NSObject <NSApplicationDelegate,AVCaptureAudioDataOutputSampleBufferDelegate>
@property NSWindow *window;
@property NSTextField *status;
@property AVCaptureSession *capture;
@property NSUInteger buffers;
@property BOOL promptOnly;
@property BOOL clockTest;
@property NSUInteger frames;
@property double previousPTS,previousDuration,maximumTimestampGap;
@property double minimumClockRate,maximumClockRate;
@property AudioDeviceID inputDevice;
@property AudioTimeStamp previousClock;
@property NSTimer *clockTimer;
@property NSMutableArray *clockRates;
@end
@implementation MicrophoneProbe
- (void)report:(NSString *)state {
    self.status.stringValue=state;
    uint32_t flags=0;csops(getpid(),0,&flags,sizeof(flags));
    NSDictionary *result=@{@"State":state,@"Authorization":@([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]),
        @"SampleBuffers":@(self.buffers),@"PlatformBinary":@((flags&0x04000000)!=0),
        @"PromptOnly":@(self.promptOnly),@"BundleIdentifier":NSBundle.mainBundle.bundleIdentifier?:@"",
        @"ProcessID":@(getpid()),@"Frames":@(self.frames),@"MaximumTimestampGap":@(self.maximumTimestampGap),
        @"MinimumClockRate":@(self.minimumClockRate),@"MaximumClockRate":@(self.maximumClockRate),
        @"ClockRates":self.clockRates?:@[],@"InputDevice":@(self.inputDevice)};
    NSData *data=[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:NULL];
    [data writeToFile:[NSString stringWithFormat:@"/tmp/vim-microphone-%u.json",getuid()] atomically:YES];
}
- (void)applicationDidFinishLaunching:(NSNotification *)note {
    self.promptOnly=[NSProcessInfo.processInfo.arguments containsObject:@"--prompt-only"];
    self.clockTest=[NSProcessInfo.processInfo.arguments containsObject:@"--clock"];
    self.window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,490,190) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable backing:NSBackingStoreBuffered defer:NO];
    self.window.title=@"Microphone Permission Test";
    self.status=[NSTextField wrappingLabelWithString:self.promptOnly?
        @"This test requests normal microphone permission. It will not open a microphone or capture any audio.":
        @"This app requests normal microphone permission, briefly checks incoming audio, and saves no recording."];
    self.status.frame=NSMakeRect(24,80,442,84);[self.window.contentView addSubview:self.status];
    NSButton *button=[NSButton buttonWithTitle:@"Test Microphone…" target:self action:@selector(request:)];button.frame=NSMakeRect(24,26,180,32);[self.window.contentView addSubview:button];
    [self.window center];[self.window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];
    [self report:@"Ready"];
    if([NSProcessInfo.processInfo.arguments containsObject:@"--request"])[self request:nil];
}
- (void)request:(id)sender {
    [self report:@"Requesting microphone permission…"];
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted){dispatch_async(dispatch_get_main_queue(),^{
        if(!granted){[self report:@"Microphone permission was not granted."];return;}
        if(self.promptOnly){[self report:@"Permission granted. No microphone was opened and no audio was captured."];return;}
        NSError *error=nil;AVCaptureDevice *device=[AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeAudio];
        AVCaptureDeviceInput *input=device?[AVCaptureDeviceInput deviceInputWithDevice:device error:&error]:nil;
        self.capture=[AVCaptureSession new];AVCaptureAudioDataOutput *output=[AVCaptureAudioDataOutput new];
        if(!input||![self.capture canAddInput:input]||![self.capture canAddOutput:output]){[self report:error.localizedDescription?:@"No microphone is available."];return;}
        [self.capture addInput:input];[self.capture addOutput:output];[output setSampleBufferDelegate:self queue:dispatch_get_main_queue()];
        self.buffers=0;self.frames=0;self.previousPTS=0;self.previousDuration=0;self.maximumTimestampGap=0;
        self.minimumClockRate=0;self.maximumClockRate=0;self.previousClock=(AudioTimeStamp){0};
        self.clockRates=[NSMutableArray array];
        AudioObjectPropertyAddress address={kAudioHardwarePropertyDefaultInputDevice,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMaster};
        UInt32 size=sizeof(_inputDevice);AudioObjectGetPropertyData(kAudioObjectSystemObject,&address,0,NULL,&size,&_inputDevice);
        [self.capture startRunning];[self report:@"Permission granted; checking audio without saving it…"];
        if(self.clockTest)self.clockTimer=[NSTimer scheduledTimerWithTimeInterval:1 target:self selector:@selector(sampleClock:) userInfo:nil repeats:YES];
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,(self.clockTest?12:2)*NSEC_PER_SEC),dispatch_get_main_queue(),^{
            [self.clockTimer invalidate];self.clockTimer=nil;
            [self.capture stopRunning];[self report:self.buffers?@"Microphone works. No recording was saved.":@"Permission granted, but no audio buffers arrived."];
        });
    });}];
}
- (void)sampleClock:(NSTimer *)timer {
    AudioTimeStamp current={0},previous=self.previousClock;
    if(AudioDeviceGetCurrentTime(self.inputDevice,&current)==noErr&&
       (current.mFlags&kAudioTimeStampSampleHostTimeValid)==kAudioTimeStampSampleHostTimeValid&&
       (previous.mFlags&kAudioTimeStampSampleHostTimeValid)==kAudioTimeStampSampleHostTimeValid&&current.mHostTime>previous.mHostTime) {
        double hz=(current.mSampleTime-previous.mSampleTime)*AudioGetHostClockFrequency()/(current.mHostTime-previous.mHostTime);
        [self.clockRates addObject:@(hz)];
        if(self.minimumClockRate==0||hz<self.minimumClockRate)self.minimumClockRate=hz;
        self.maximumClockRate=MAX(self.maximumClockRate,hz);
    }
    self.previousClock=current;[self report:@"Measuring input clock; no audio recording is saved…"];
}
- (void)captureOutput:(AVCaptureOutput *)output didOutputSampleBuffer:(CMSampleBufferRef)buffer fromConnection:(AVCaptureConnection *)connection {
    self.buffers++;self.frames+=CMSampleBufferGetNumSamples(buffer);
    double pts=CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(buffer)),duration=CMTimeGetSeconds(CMSampleBufferGetDuration(buffer));
    if(self.previousDuration>0&&isfinite(pts))self.maximumTimestampGap=MAX(self.maximumTimestampGap,fabs(pts-self.previousPTS-self.previousDuration));
    self.previousPTS=pts;self.previousDuration=isfinite(duration)?duration:0;
}
@end
int main(int argc,const char **argv){@autoreleasepool {NSApplication *app=NSApplication.sharedApplication;MicrophoneProbe *delegate=[MicrophoneProbe new];app.delegate=delegate;[app setActivationPolicy:NSApplicationActivationPolicyRegular];[app run];return 0;}}
