// Finite read-only decode diagnostic. Compare AVFoundation's reader with
// QTKit and actual playback; a decoded buffer is not presentation evidence.
#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#include <math.h>
#include <unistd.h>

int main(int argc,const char *argv[]) { @autoreleasepool {
    if(argc!=3&&argc!=4){fputs("usage: avfoundation-movie-frame MOVIE SECONDS [FRAME_COUNT=1]\n",stderr);return 64;}
    char *end=NULL;double second=strtod(argv[2],&end);
    if(!end||*end||!isfinite(second)||second<0)return 64;
    unsigned long requested=1;
    if(argc==4){end=NULL;requested=strtoul(argv[3],&end,10);if(!end||end==argv[3]||*end||requested<1||requested>300)return 64;}
    alarm(90);setvbuf(stdout,NULL,_IONBF,0);
    AVURLAsset *asset=[AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:@(argv[1])] options:nil];
    NSArray *tracks=[asset tracksWithMediaType:AVMediaTypeVideo];
    if(!tracks.count){puts("No video track");return 1;}
    for(AVAssetTrack *track in [asset tracks]){
        printf("track=%d type=%s start=%.6f duration=%.6f formats=",
            (int)track.trackID,track.mediaType.UTF8String,CMTimeGetSeconds(track.timeRange.start),
            CMTimeGetSeconds(track.timeRange.duration));
        for(id description in track.formatDescriptions)
            printf("%08x ",(unsigned)CMFormatDescriptionGetMediaSubType((CMFormatDescriptionRef)description));
        putchar('\n');
    }
    NSError *error=nil;
    AVAssetReader *reader=[[[AVAssetReader alloc] initWithAsset:asset error:&error] autorelease];
    if(!reader){printf("Reader error=%s/%ld\n",error.domain.UTF8String,(long)error.code);return 1;}
    CMTime start=CMTimeMakeWithSeconds(second,600);
    reader.timeRange=CMTimeRangeMake(start,CMTimeSubtract(asset.duration,start));
    NSDictionary *settings=@{(id)kCVPixelBufferPixelFormatTypeKey:@(kCVPixelFormatType_32BGRA)};
    AVAssetReaderTrackOutput *output=[AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:tracks[0] outputSettings:settings];
    [reader addOutput:output];
    puts("Starting native AVFoundation BGRA decode");
    CFAbsoluteTime began=CFAbsoluteTimeGetCurrent();unsigned long decoded=0;
    BOOL started=[reader startReading];
    do {
    CMSampleBufferRef sample=started?[output copyNextSampleBuffer]:NULL;
    CVPixelBufferRef pixels=sample?CMSampleBufferGetImageBuffer(sample):NULL;
    int result=1;
    if(pixels&&CVPixelBufferGetPixelFormatType(pixels)==kCVPixelFormatType_32BGRA&&
       !CVPixelBufferIsPlanar(pixels)&&!CVPixelBufferLockBaseAddress(pixels,kCVPixelBufferLock_ReadOnly)) {
        size_t width=CVPixelBufferGetWidth(pixels),height=CVPixelBufferGetHeight(pixels);
        size_t row=CVPixelBufferGetBytesPerRow(pixels),nonblack=0;uint64_t sum=0;
        const unsigned char *base=CVPixelBufferGetBaseAddress(pixels);
        if(!decoded||decoded+1==requested)for(size_t y=0;y<height;++y)for(size_t x=0;x<width;++x){
            const unsigned char *p=base+y*row+x*4;
            nonblack+=p[0]>8||p[1]>8||p[2]>8;sum+=p[0]+p[1]+p[2];
        }
        if(!decoded||decoded+1==requested)printf("frame=%zux%zu time=%.3f nonblack=%zu/%zu rgb-mean=%.3f\n",width,height,
            CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sample)),nonblack,width*height,
            width&&height?(double)sum/(width*height*3):0);
        CVPixelBufferUnlockBaseAddress(pixels,kCVPixelBufferLock_ReadOnly);result=0;
    }else {
        printf("Decode failed started=%d status=%ld error=%s/%ld\n",started,(long)reader.status,
            reader.error.domain.UTF8String?reader.error.domain.UTF8String:"none",(long)reader.error.code);
        if(reader.error)printf("Native error details: %s\n",reader.error.description.UTF8String);
    }
    if(sample)CFRelease(sample);
    if(result){[reader cancelReading];return result;}
    decoded++;
    } while(decoded<requested);
    double elapsed=CFAbsoluteTimeGetCurrent()-began;
    printf("decoded=%lu elapsed=%.6f frames-per-second=%.3f (offline decoder, not display FPS)\n",decoded,elapsed,elapsed>0?decoded/elapsed:0);
    [reader cancelReading];return 0;
} }
