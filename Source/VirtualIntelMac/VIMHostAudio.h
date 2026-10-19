#import <Foundation/Foundation.h>
@import CoreAudio;
#include <stddef.h>
#include <math.h>

// Read-only, once per VM launch. No stream/microphone access or device changes.
static inline NSUInteger VIMHostAudioSampleRate(NSString *uid,BOOL input) {
    AudioDeviceID device=kAudioObjectUnknown;UInt32 size=sizeof(device);
    if(!uid.length||[uid isEqual:@"default"]||[uid isEqual:@"disabled"]) {
        AudioObjectPropertyAddress address={input?kAudioHardwarePropertyDefaultInputDevice:kAudioHardwarePropertyDefaultOutputDevice,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
        if(AudioObjectGetPropertyData(kAudioObjectSystemObject,&address,0,NULL,&size,&device)!=noErr)return 48000;
    } else {
        CFStringRef value=(__bridge CFStringRef)uid;
        AudioValueTranslation translation={&value,sizeof(value),&device,sizeof(device)};size=sizeof(translation);
        AudioObjectPropertyAddress address={kAudioHardwarePropertyDeviceForUID,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
        if(AudioObjectGetPropertyData(kAudioObjectSystemObject,&address,0,NULL,&size,&translation)!=noErr)return 48000;
    }
    Float64 rate=0;size=sizeof(rate);
    AudioObjectPropertyAddress address={kAudioDevicePropertyNominalSampleRate,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    if(device==kAudioObjectUnknown||AudioObjectGetPropertyData(device,&address,0,NULL,&size,&rate)!=noErr||
       !isfinite(rate)||rate<8000||rate>192000||fabs(rate-round(rate))>.01)return 48000;
    return (NSUInteger)llround(rate);
}
static inline NSUInteger VIMHostOutputSampleRate(NSString *uid) {return VIMHostAudioSampleRate(uid,NO);}
static inline NSUInteger VIMHostInputSampleRate(NSString *uid) {return VIMHostAudioSampleRate(uid,YES);}

// Enumeration does not open a stream or request microphone permission. Persist
// Core Audio UIDs, never transient numeric device IDs or translated labels.
static inline NSArray<NSDictionary<NSString *,NSString *> *> *VIMHostAudioDevices(BOOL input) {
    AudioObjectPropertyAddress address={kAudioHardwarePropertyDevices,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
    UInt32 size=0;
    if(AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,&address,0,NULL,&size)!=noErr||size>1024*1024)return @[];
    NSMutableData *storage=[NSMutableData dataWithLength:size];
    if(AudioObjectGetPropertyData(kAudioObjectSystemObject,&address,0,NULL,&size,storage.mutableBytes)!=noErr)return @[];
    NSMutableArray *devices=[NSMutableArray new];AudioDeviceID *ids=storage.mutableBytes;
    for(NSUInteger i=0;i<size/sizeof(AudioDeviceID);i++) {
        AudioObjectPropertyAddress streams={kAudioDevicePropertyStreamConfiguration,input?kAudioDevicePropertyScopeInput:kAudioDevicePropertyScopeOutput,kAudioObjectPropertyElementMain};
        UInt32 length=0;
        if(AudioObjectGetPropertyDataSize(ids[i],&streams,0,NULL,&length)!=noErr||length<sizeof(AudioBufferList)||length>1024*1024)continue;
        NSMutableData *buffers=[NSMutableData dataWithLength:length];
        if(AudioObjectGetPropertyData(ids[i],&streams,0,NULL,&length,buffers.mutableBytes)!=noErr)continue;
        AudioBufferList *list=buffers.mutableBytes;UInt32 channels=0;
        if(list->mNumberBuffers>(length - offsetof(AudioBufferList,mBuffers))/sizeof(AudioBuffer))continue;
        for(UInt32 j=0;j<list->mNumberBuffers;j++)channels+=list->mBuffers[j].mNumberChannels;
        if(!channels)continue;
        AudioObjectPropertyAddress nameAddress={kAudioObjectPropertyName,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
        AudioObjectPropertyAddress uidAddress={kAudioDevicePropertyDeviceUID,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
        CFStringRef name=NULL,uid=NULL;UInt32 nameSize=sizeof(name),uidSize=sizeof(uid);
        AudioObjectGetPropertyData(ids[i],&nameAddress,0,NULL,&nameSize,&name);
        AudioObjectGetPropertyData(ids[i],&uidAddress,0,NULL,&uidSize,&uid);
        NSString *label=CFBridgingRelease(name),*identifier=CFBridgingRelease(uid);
        // SDL's Core Audio enumerator trims trailing ASCII spaces as well.
        while([label hasSuffix:@" "])label=[label substringToIndex:label.length-1];
        if(label.length&&identifier.length)[devices addObject:@{@"Name":label,@"UID":identifier}];
    }
    return [devices sortedArrayUsingComparator:^NSComparisonResult(NSDictionary *a,NSDictionary *b){return [a[@"Name"] localizedStandardCompare:b[@"Name"]];}];
}
static inline NSString *VIMAudioSelection(NSDictionary *configuration,BOOL input) {
    if(configuration[@"AudioEnabled"]&&![configuration[@"AudioEnabled"] boolValue])return @"disabled";
    return configuration[input?@"AudioInputDevice":@"AudioOutputDevice"]?:@"default";
}
static inline NSString *VIMDisplayMode(NSDictionary *configuration) {
    return configuration[@"DisplayMode"]?:(configuration[@"RetinaEnabled"]&&![configuration[@"RetinaEnabled"] boolValue]?@"scaled":@"retina");
}
