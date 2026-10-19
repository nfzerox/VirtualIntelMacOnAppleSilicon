/* Build: clang -mmacosx-version-min=11.0 -arch x86_64 -arch arm64
 * qemu-audio-smoke.c -framework AudioToolbox -framework CoreAudio
 * -framework CoreFoundation -o qemu-audio-smoke
 * Guest build: replace the deployment/architecture flags with
 * -mmacosx-version-min=10.14 -arch x86_64. No audio is retained. */
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreAudio/HostTime.h>
#include <CoreFoundation/CoreFoundation.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned long long frames, nonzero;
static double energy;
static unsigned phase;
static void output(void *ctx, AudioQueueRef q, AudioQueueBufferRef b) {
    (void)ctx;
    float *p = b->mAudioData;
    unsigned n = b->mAudioDataBytesCapacity / sizeof(float);
    for (unsigned i = 0; i < n; i++)
        p[i] = 0.05f * sinf(2.0f * 3.14159265f * 440.0f * (phase++ % 48000) / 48000.0f);
    b->mAudioDataByteSize = n * sizeof(float);
    frames += n;
    AudioQueueEnqueueBuffer(q, b, 0, NULL);
}
static void input(void *ctx, AudioQueueRef q, AudioQueueBufferRef b,
                  const AudioTimeStamp *ts, UInt32 packets,
                  const AudioStreamPacketDescription *desc) {
    (void)ctx; (void)ts; (void)packets; (void)desc;
    const float *p = b->mAudioData;
    unsigned n = b->mAudioDataByteSize / sizeof(float);
    for (unsigned i = 0; i < n; i++) {
        energy += (double)p[i] * p[i];
        nonzero += fabsf(p[i]) > 0.00001f;
    }
    frames += n;
    AudioQueueEnqueueBuffer(q, b, 0, NULL);
}
int main(int argc, char **argv) {
    alarm(15);
    int seconds = argc == 3 ? atoi(argv[2]) : 3;
    if ((argc != 2 && argc != 3) || seconds < 1 || seconds > 10 ||
        (strcmp(argv[1], "play") && strcmp(argv[1], "play-clock") && strcmp(argv[1], "record"))) {
        fprintf(stderr, "usage: %s play|play-clock|record [seconds: 1–10, default 3]\n", argv[0]);
        return 64;
    }
    int rec = !strcmp(argv[1], "record");
    int clock = !strcmp(argv[1], "play-clock");
    AudioObjectPropertyAddress a = {
        rec ? kAudioHardwarePropertyDefaultInputDevice : kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMaster
    };
    AudioDeviceID dev = 0;
    UInt32 size = sizeof(dev);
    OSStatus err = AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, NULL, &size, &dev);
    printf("default_%s_device=%u status=%d\n", rec ? "input" : "output", dev, (int)err);
    if (err || !dev) return 1;
    AudioStreamBasicDescription fmt = {0};
    fmt.mSampleRate = 48000;
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kAudioFormatFlagsNativeFloatPacked;
    fmt.mBytesPerPacket = fmt.mBytesPerFrame = sizeof(float);
    fmt.mFramesPerPacket = fmt.mChannelsPerFrame = 1;
    fmt.mBitsPerChannel = 32;
    AudioQueueRef queue = NULL;
    err = rec ? AudioQueueNewInput(&fmt, input, NULL, CFRunLoopGetCurrent(), kCFRunLoopCommonModes, 0, &queue)
              /* Playback callbacks belong on AudioQueue's audio-management
               * thread, not this CLI's ordinary-priority main run loop. */
              : AudioQueueNewOutput(&fmt, output, NULL, NULL, NULL, 0, &queue);
    if (err) { printf("queue_create=%d\n", (int)err); return 2; }
    for (int i = 0; i < 3; i++) {
        AudioQueueBufferRef b;
        err = AudioQueueAllocateBuffer(queue, 4096, &b);
        if (err) return 3;
        if (rec) AudioQueueEnqueueBuffer(queue, b, 0, NULL);
        else output(NULL, queue, b);
    }
    unsigned long long primed = frames;
    err = AudioQueueStart(queue, NULL);
    printf("queue_start=%d\n", (int)err);
    if (!err) {
        if (rec) CFRunLoopRunInMode(kCFRunLoopDefaultMode, seconds, false);
        else if (!clock) sleep(seconds); /* No playback main-loop source. */
        else {
            AudioTimeStamp previous = {0};
            for (int i = 0; i < seconds; i++) {
                sleep(1);
                AudioTimeStamp current = {0};
                OSStatus status = AudioDeviceGetCurrentTime(dev, &current);
                if (!status && (current.mFlags & kAudioTimeStampSampleHostTimeValid) == kAudioTimeStampSampleHostTimeValid &&
                    (previous.mFlags & kAudioTimeStampSampleHostTimeValid) == kAudioTimeStampSampleHostTimeValid &&
                    current.mHostTime > previous.mHostTime) {
                    printf("device_clock_hz=%.3f sample_delta=%.0f\n",
                        (current.mSampleTime - previous.mSampleTime) * AudioGetHostClockFrequency() /
                        (current.mHostTime - previous.mHostTime), current.mSampleTime - previous.mSampleTime);
                } else printf("device_clock_status=%d flags=%u\n", (int)status,(unsigned)current.mFlags);
                previous = current;
            }
        }
    }
    AudioQueueStop(queue, true);
    AudioQueueDispose(queue, true);
    printf("callback_frames=%llu nonzero=%llu rms=%.8f\n",
           frames - primed, nonzero, frames ? sqrt(energy / frames) : 0);
    if (rec && !err && frames != primed && energy == 0.0) {
        fprintf(stderr, "Only silence was captured. Check microphone permission, input device and gain; callback delivery alone does not qualify recording.\n");
        if (geteuid() == 0)
            fprintf(stderr, "A root guest-agent test may be attributed to a background service by guest privacy controls. Qualify a permitted desktop app separately.\n");
        return 5;
    }
    return err || frames == primed ? 4 : 0;
}
