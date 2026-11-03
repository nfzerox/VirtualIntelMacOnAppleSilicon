/* Cold first-install routing, not an always-running policy. Never overrides a
 * user's subsequent device selection on ordinary VM boots. */
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
static unsigned Channels(AudioDeviceID device,int input) {
    AudioObjectPropertyAddress address={kAudioDevicePropertyStreamConfiguration,input?kAudioDevicePropertyScopeInput:kAudioDevicePropertyScopeOutput,kAudioObjectPropertyElementMaster};
    UInt32 size=0;if(AudioObjectGetPropertyDataSize(device,&address,0,NULL,&size)||size<sizeof(AudioBufferList)||size>65536)return 0;
    AudioBufferList *list=calloc(1,size);if(!list)return 0;
    unsigned channels=0;
    if(!AudioObjectGetPropertyData(device,&address,0,NULL,&size,list)&&size>=offsetof(AudioBufferList,mBuffers)&&list->mNumberBuffers<=(size - offsetof(AudioBufferList,mBuffers))/sizeof(AudioBuffer))
        for(UInt32 i=0;i<list->mNumberBuffers;i++)channels+=list->mBuffers[i].mNumberChannels;
    free(list);return channels;
}
static int IsVoodoo(AudioDeviceID device) {
    AudioObjectPropertyAddress address={kAudioDevicePropertyModelUID,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMaster};
    CFStringRef model=NULL;UInt32 size=sizeof(model);
    if(AudioObjectGetPropertyData(device,&address,0,NULL,&size,&model)||!model)return 0;
    int matches=CFStringHasPrefix(model,CFSTR("VoodooHDA:"));CFRelease(model);return matches;
}
int main(int argc,char **argv) {
    if(argc>2||(argc==2&&strcmp(argv[1],"--set-defaults")))return 64;
    io_service_t guard=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("VMsvga2"));
    if(!guard)return 77;IOObjectRelease(guard);
    AudioObjectPropertyAddress address={kAudioHardwarePropertyDevices,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMaster};
    UInt32 size=0;if(AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,&address,0,NULL,&size)||size>65536)return 1;
    AudioDeviceID *devices=calloc(1,size);if(!devices)return 1;
    if(AudioObjectGetPropertyData(kAudioObjectSystemObject,&address,0,NULL,&size,devices)){free(devices);return 1;}
    AudioDeviceID input=0,output=0;
    for(unsigned i=0;i<size/sizeof(*devices);i++)if(IsVoodoo(devices[i])){
        unsigned in=Channels(devices[i],1),out=Channels(devices[i],0);
        printf("device=%u input_channels=%u output_channels=%u\n",(unsigned)devices[i],in,out);
        if(in){if(input){free(devices);return 2;}input=devices[i];}
        if(out){if(output){free(devices);return 2;}output=devices[i];}
    }
    free(devices);if(!input||!output)return 3;
    const AudioObjectPropertySelector properties[]={kAudioHardwarePropertyDefaultOutputDevice,kAudioHardwarePropertyDefaultSystemOutputDevice,kAudioHardwarePropertyDefaultInputDevice};
    for(unsigned i=0;i<3;i++){
        address.mSelector=properties[i];AudioDeviceID selected=i==2?input:output;OSStatus status=0;
        if(argc==2)status=AudioObjectSetPropertyData(kAudioObjectSystemObject,&address,0,NULL,sizeof(selected),&selected);
        AudioDeviceID actual=0;UInt32 bytes=sizeof(actual);
        OSStatus readback=AudioObjectGetPropertyData(kAudioObjectSystemObject,&address,0,NULL,&bytes,&actual);
        printf("selector=%08x selected=%u actual=%u set_status=%d read_status=%d\n",(unsigned)properties[i],(unsigned)selected,(unsigned)actual,(int)status,(int)readback);
        if(status||readback||(argc==2&&actual!=selected))return 4;
    }
    return 0;
}
