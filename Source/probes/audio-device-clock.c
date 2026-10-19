// Metadata only: no audio stream, microphone access, routing or rate changes.
#include <CoreAudio/CoreAudio.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
int main(int argc,char **argv) {
    if(argc!=2||(strcmp(argv[1],"input")&&strcmp(argv[1],"output")))return 64;
    AudioDeviceID device=0;UInt32 bytes=sizeof(device);
    AudioObjectPropertyAddress address={!strcmp(argv[1],"input")?kAudioHardwarePropertyDefaultInputDevice:kAudioHardwarePropertyDefaultOutputDevice,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMaster};
    if(AudioObjectGetPropertyData(kAudioObjectSystemObject,&address,0,NULL,&bytes,&device))return 1;
    mach_timebase_info_data_t timebase;mach_timebase_info(&timebase);
    printf("device=%u frequency=%.3f timebase=%u/%u\n",device,AudioGetHostClockFrequency(),timebase.numer,timebase.denom);
    AudioTimeStamp previous={0};setbuf(stdout,NULL);
    for(unsigned i=0;i<10;i++) {
        AudioTimeStamp now={0};OSStatus status=AudioDeviceGetCurrentTime(device,&now);
        if(!status&&i&&now.mHostTime>previous.mHostTime) {
            double seconds=(now.mHostTime-previous.mHostTime)/(double)AudioGetHostClockFrequency();
            printf("samples=%.3f seconds=%.9f hz=%.3f flags=%u\n",now.mSampleTime-previous.mSampleTime,seconds,(now.mSampleTime-previous.mSampleTime)/seconds,(unsigned)now.mFlags);
        } else printf("status=%d flags=%u sample=%.3f\n",(int)status,(unsigned)now.mFlags,now.mSampleTime);
        previous=now;sleep(1);
    }
    return 0;
}
