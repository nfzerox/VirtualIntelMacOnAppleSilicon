// Bounded guest-only public HAL buffer experiment. No stream/microphone capture.
#import <Foundation/Foundation.h>
#import <CoreAudio/CoreAudio.h>
#include <unistd.h>
int main(int argc,char **argv) {@autoreleasepool {
    if(argc>2)return 64;
    NSString *version=[NSDictionary dictionaryWithContentsOfFile:@"/System/Library/CoreServices/SystemVersion.plist"][@"ProductVersion"];
    if(!([version hasPrefix:@"10.14."]||[version hasPrefix:@"10.15."]))return 77;
    AudioDeviceID device=0;UInt32 bytes=sizeof(device);
    AudioObjectPropertyAddress address={kAudioHardwarePropertyDefaultOutputDevice,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMaster};
    OSStatus status=AudioObjectGetPropertyData(kAudioObjectSystemObject,&address,0,NULL,&bytes,&device);
    if(status||!device)return 1;
    address.mSelector=kAudioDevicePropertyBufferFrameSize;
    UInt32 original=0;bytes=sizeof(original);Boolean settable=false;
    status=AudioObjectGetPropertyData(device,&address,0,NULL,&bytes,&original);
    if(status||AudioObjectIsPropertySettable(device,&address,&settable))return 1;
    AudioObjectPropertyAddress rangeAddress=address;rangeAddress.mSelector=kAudioDevicePropertyBufferFrameSizeRange;
    AudioValueRange range={0};bytes=sizeof(range);
    if(AudioObjectGetPropertyData(device,&rangeAddress,0,NULL,&bytes,&range))return 1;
    UInt32 target=original;
    if(argc==2) {
        char *end=NULL;unsigned long parsed=strtoul(argv[1],&end,10);
        if(geteuid()!=0||!*argv[1]||*end||!settable||parsed<range.mMinimum||parsed>range.mMaximum||parsed<64||parsed>4096)return 77;
        target=(UInt32)parsed;
        status=AudioObjectSetPropertyData(device,&address,0,NULL,sizeof(target),&target);
    }
    UInt32 observed=0;bytes=sizeof(observed);
    OSStatus readback=AudioObjectGetPropertyData(device,&address,0,NULL,&bytes,&observed);
    NSDictionary *result=@{@"Device":@(device),@"OriginalFrames":@(original),@"ObservedFrames":@(observed),
        @"Minimum":@(range.mMinimum),@"Maximum":@(range.mMaximum),@"Settable":@(settable!=0),@"Status":@(status),@"ReadbackStatus":@(readback)};
    NSData *json=[NSJSONSerialization dataWithJSONObject:result options:0 error:NULL];
    puts([[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding].UTF8String);
    return status||readback||target!=observed?1:0;
}}
