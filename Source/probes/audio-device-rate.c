// Bounded CoreAudio device-rate diagnostic; does not modify persistent defaults.
#include <CoreAudio/CoreAudio.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    if (argc != 2) return 64;
    Float64 rate = strtod(argv[1], NULL);
    if (rate != 44100 && rate != 48000) return 64;
    AudioObjectPropertyAddress address = {kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMaster};
    AudioDeviceID device = 0; UInt32 size = sizeof(device);
    OSStatus status = AudioObjectGetPropertyData(kAudioObjectSystemObject,
        &address, 0, NULL, &size, &device);
    if (status || !device) return 1;
    address.mSelector = kAudioDevicePropertyNominalSampleRate;
    status = AudioObjectSetPropertyData(device, &address, 0, NULL, sizeof(rate), &rate);
    printf("device=%u rate=%.0f status=%d\n", device, rate, (int)status);
    return status ? 2 : 0;
}
