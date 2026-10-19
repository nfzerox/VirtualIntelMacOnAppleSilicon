// Read-only default-output metadata and bounded overload notifications.
// Never opens a stream, changes routing/rate, or reads microphone/audio data.
#import <Foundation/Foundation.h>
#import <CoreAudio/CoreAudio.h>
#include <time.h>

static double Now(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}
static BOOL Read(AudioObjectID object, AudioObjectPropertySelector selector,
                 AudioObjectPropertyScope scope, void *value, UInt32 bytes) {
    AudioObjectPropertyAddress address={selector,scope,kAudioObjectPropertyElementMaster};
    UInt32 size=bytes;
    return AudioObjectGetPropertyData(object,&address,0,NULL,&size,value)==noErr&&size==bytes;
}
static void Emit(NSDictionary *record) {
    NSData *data=[NSJSONSerialization dataWithJSONObject:record options:0 error:nil];
    puts([[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding].UTF8String);
    fflush(stdout);
}
int main(int argc,char **argv) {
    @autoreleasepool {
        char *end=NULL;long duration=argc==2?strtol(argv[1],&end,10):0;
        if(argc!=2||!argv[1][0]||*end||duration<1||duration>60)return 64;
        AudioDeviceID device=0;
        if(!Read(kAudioObjectSystemObject,kAudioHardwarePropertyDefaultOutputDevice,
                 kAudioObjectPropertyScopeGlobal,&device,sizeof(device))||!device)return 1;
        NSMutableDictionary *metadata=[@{@"Device":@(device),@"Seconds":@(Now()),
            @"AudioDataCaptured":@NO,@"DeviceSettingsChanged":@NO} mutableCopy];
        CFStringRef name=NULL;
        if(Read(device,kAudioObjectPropertyName,kAudioObjectPropertyScopeGlobal,&name,sizeof(name)))
            metadata[@"Name"]=CFBridgingRelease(name);
        Float64 rate=0;
        if(Read(device,kAudioDevicePropertyNominalSampleRate,kAudioObjectPropertyScopeGlobal,&rate,sizeof(rate)))
            metadata[@"NominalRate"]=@(rate);
        for(NSDictionary *property in @[
            @{@"Key":@"BufferFrames",@"Selector":@(kAudioDevicePropertyBufferFrameSize),@"Scope":@(kAudioObjectPropertyScopeGlobal)},
            @{@"Key":@"LatencyFrames",@"Selector":@(kAudioDevicePropertyLatency),@"Scope":@(kAudioDevicePropertyScopeOutput)},
            @{@"Key":@"SafetyFrames",@"Selector":@(kAudioDevicePropertySafetyOffset),@"Scope":@(kAudioDevicePropertyScopeOutput)},
            @{@"Key":@"Running",@"Selector":@(kAudioDevicePropertyDeviceIsRunningSomewhere),@"Scope":@(kAudioObjectPropertyScopeGlobal)}]) {
            UInt32 value=0;
            if(Read(device,[property[@"Selector"] unsignedIntValue],[property[@"Scope"] unsignedIntValue],&value,sizeof(value)))
                metadata[property[@"Key"]]=@(value);
        }
        Emit(metadata);
        __block unsigned overloads=0;
        AudioObjectPropertyAddress address={kAudioDeviceProcessorOverload,
            kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMaster};
        // Main-queue delivery, not an IOProc: no logging/allocation on the device's
        // realtime audio thread. Counts only, no sample content or other clients.
        AudioObjectPropertyListenerBlock listener=^(UInt32 count,const AudioObjectPropertyAddress *addresses){
            (void)count;(void)addresses;
            overloads++;
            if(overloads<=20)Emit(@{@"Overload":@(overloads),@"Seconds":@(Now())});
        };
        OSStatus status=AudioObjectAddPropertyListenerBlock(device,&address,dispatch_get_main_queue(),listener);
        Emit(@{@"ListenerStatus":@(status)});
        if(status)return 2;
        NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:duration];
        while(deadline.timeIntervalSinceNow>0)
            [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
        AudioObjectRemovePropertyListenerBlock(device,&address,dispatch_get_main_queue(),listener);
        Emit(@{@"Overloads":@(overloads),@"Seconds":@(Now()),@"Duration":@(duration)});
        return 0;
    }
}
