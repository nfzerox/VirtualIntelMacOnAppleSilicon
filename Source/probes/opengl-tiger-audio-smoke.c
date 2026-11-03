/* Finite native10.4 HAL playback/input fixture. No AudioQueue (10.5+),
 * saved recording, logging/allocation in callbacks, or device-format changes.
 * Input is reduced to aggregate signal statistics and never retained. */
#include <CoreAudio/CoreAudio.h>
#include <CoreAudio/HostTime.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    int input;
    double rate,energy,peak;
    unsigned phase;
    unsigned long long samples,nonzero,callbacks;
} TigerAudioState;
static OSStatus TigerAudioIO(AudioDeviceID device,const AudioTimeStamp *now,
    const AudioBufferList *input,const AudioTimeStamp *inputTime,
    AudioBufferList *output,const AudioTimeStamp *outputTime,void *context) {
    (void)device;(void)now;(void)inputTime;(void)outputTime;
    TigerAudioState *state=context;state->callbacks++;
    if(state->input){
        if(input)for(UInt32 b=0;b<input->mNumberBuffers;b++){
            const AudioBuffer *buffer=&input->mBuffers[b];
            const float *values=buffer->mData;if(!values)continue;
            UInt32 count=buffer->mDataByteSize/sizeof(float);
            for(UInt32 i=0;i<count;i++){
                double value=values[i],magnitude=fabs(value);
                state->energy+=value*value;if(magnitude>state->peak)state->peak=magnitude;
                state->nonzero+=magnitude>0.00001;state->samples++;
            }
        }
    }else if(output){
        unsigned first=state->phase,advanced=0;
        for(UInt32 b=0;b<output->mNumberBuffers;b++){
            AudioBuffer *buffer=&output->mBuffers[b];float *values=buffer->mData;
            UInt32 channels=buffer->mNumberChannels;
            if(!values||!channels)continue;
            UInt32 frames=buffer->mDataByteSize/(sizeof(float)*channels);
            for(UInt32 frame=0;frame<frames;frame++){
                float value=(float)(0.04*sin(2.0*3.141592653589793*440.0*(first+frame)/state->rate));
                for(UInt32 channel=0;channel<channels;channel++)values[frame*channels+channel]=value;
            }
            if(frames>advanced)advanced=frames;
            state->samples+=(unsigned long long)frames*channels;
        }
        state->phase=first+advanced;
    }
    return noErr;
}
int main(int argc,char **argv) {
    if(argc!=2||(strcmp(argv[1],"play")&&strcmp(argv[1],"record")))return 64;
    TigerAudioState state={0};state.input=!strcmp(argv[1],"record");
    AudioObjectPropertyAddress address={state.input?kAudioHardwarePropertyDefaultInputDevice:kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMaster};
    AudioDeviceID device=0;UInt32 bytes=sizeof(device);
    OSStatus result=AudioObjectGetPropertyData(kAudioObjectSystemObject,&address,0,NULL,&bytes,&device);
    if(result||!device){printf("default_device_status=%d\n",(int)result);return 1;}
    AudioStreamBasicDescription format={0};bytes=sizeof(format);
    result=AudioDeviceGetProperty(device,0,state.input,kAudioDevicePropertyStreamFormat,&bytes,&format);
    printf("device=%u input=%d format_status=%d rate=%.0f channels=%u flags=%08x bits=%u\n",
        (unsigned)device,state.input,(int)result,format.mSampleRate,(unsigned)format.mChannelsPerFrame,
        (unsigned)format.mFormatFlags,(unsigned)format.mBitsPerChannel);
    if(result||format.mFormatID!=kAudioFormatLinearPCM||!(format.mFormatFlags&kAudioFormatFlagIsFloat)||
        format.mBitsPerChannel!=32||format.mSampleRate<8000||format.mSampleRate>192000)return 2;
    state.rate=format.mSampleRate;
    result=AudioDeviceAddIOProc(device,TigerAudioIO,&state);
    if(result){printf("add_ioproc_status=%d\n",(int)result);return 3;}
    result=AudioDeviceStart(device,TigerAudioIO);printf("start_status=%d\n",(int)result);
    if(!result){
        for(unsigned second=0;second<3;second++){
            AudioTimeStamp before={0},after={0};
            OSStatus first=AudioDeviceGetCurrentTime(device,&before);sleep(1);
            OSStatus last=AudioDeviceGetCurrentTime(device,&after);
            if(!first&&!last&&after.mHostTime>before.mHostTime&&
                (before.mFlags&kAudioTimeStampSampleHostTimeValid)==kAudioTimeStampSampleHostTimeValid&&
                (after.mFlags&kAudioTimeStampSampleHostTimeValid)==kAudioTimeStampSampleHostTimeValid)
                printf("device_clock_hz=%.3f\n",(after.mSampleTime-before.mSampleTime)*AudioGetHostClockFrequency()/(after.mHostTime-before.mHostTime));
        }
        AudioDeviceStop(device,TigerAudioIO);
    }
    AudioDeviceRemoveIOProc(device,TigerAudioIO);
    printf("callbacks=%llu samples=%llu nonzero=%llu rms=%.8f peak=%.8f\n",
        state.callbacks,state.samples,state.nonzero,state.samples?sqrt(state.energy/state.samples):0,state.peak);
    if(result||!state.callbacks||!state.samples)return 4;
    return state.input&&!state.nonzero?5:0;
}
