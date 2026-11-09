/* Bounded native Mavericks effect reference. CGDisplayStream + streamed video
 * avoids the several-hundred-ms synchronous PNG readback in the old capture
 * burst. Diagnostic only: scaled H.264 is visual evidence, not exact pixels,
 * uninstrumented FPS, or a production capture/polling service. */
#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <ApplicationServices/ApplicationServices.h>
#import <CoreVideo/CoreVideo.h>
#include <mach/mach_time.h>
#include <math.h>
#include <string.h>
#include <unistd.h>

/* Use the native display's pixels and native delivery cadence, with no
 * frame-time cap, CPU pixel access, video encoder, or logging in the callback.
 * These are WindowServer display-stream timestamps, not proof of scanout. */
static int recordTiming(double seconds)
{
    CGDirectDisplayID display=CGMainDisplayID();
    CGDisplayModeRef mode=CGDisplayCopyDisplayMode(display);
    if(!mode)return 1;
    size_t width=CGDisplayModeGetPixelWidth(mode),height=CGDisplayModeGetPixelHeight(mode);
    double refresh=CGDisplayModeGetRefreshRate(mode);
    CGDisplayModeRelease(mode);
    if(!width||!height)return 1;
    CGRect bounds=CGDisplayBounds(display);
    mach_timebase_info_data_t timebase;
    if(mach_timebase_info(&timebase)!=KERN_SUCCESS || !timebase.denom)return 1;
    double scale=(double)timebase.numer/timebase.denom/1e9;
    static struct { uint64_t scheduled,delivered;size_t dropped; } frames[4096];
    __block size_t count=0,overflow=0,blank=0,missing=0;
    dispatch_queue_t queue=dispatch_queue_create("org.vim.research.native-display-timing",DISPATCH_QUEUE_SERIAL);
    CGDisplayStreamRef stream=CGDisplayStreamCreateWithDispatchQueue(display,width,height,'BGRA',
        (__bridge CFDictionaryRef)@{(__bridge NSString *)kCGDisplayStreamShowCursor:@NO},queue,
        ^(CGDisplayStreamFrameStatus status,uint64_t time,IOSurfaceRef surface,CGDisplayStreamUpdateRef update){
            uint64_t now=mach_absolute_time();
            if(status==kCGDisplayStreamFrameStatusFrameComplete){
                if(!time||!surface||!update){++missing;return;}
                if(count==sizeof(frames)/sizeof(frames[0])){++overflow;return;}
                frames[count].scheduled=time;frames[count].delivered=now;
                frames[count].dropped=CGDisplayStreamUpdateGetDropCount(update);++count;
            }else if(status==kCGDisplayStreamFrameStatusFrameBlank)++blank;
            else if(status==kCGDisplayStreamFrameStatusStopped){
                puts("WindowServerScheduledAt,StreamDeliveredAt,StreamDropped");
                size_t dropped=0,nonmonotonic=0;
                for(size_t i=0;i<count;++i){
                    if(i && frames[i].scheduled<=frames[i-1].scheduled)++nonmonotonic;
                    dropped+=frames[i].dropped;
                    printf("%.9f,%.9f,%zu\n",frames[i].scheduled*scale,frames[i].delivered*scale,frames[i].dropped);
                }
                fprintf(stderr,"native display frames=%zu dropped=%zu overflow=%zu blank=%zu missing=%zu nonmonotonic=%zu\n",
                        count,dropped,overflow,blank,missing,nonmonotonic);
                exit(count&&!dropped&&!overflow&&!blank&&!missing&&!nonmonotonic?0:1);
            }
        });
    if(!stream)return 1;
    if(CGDisplayStreamStart(stream)){CFRelease(stream);return 1;}
    fprintf(stderr,"READY native display timing %zux%zu pixels %.0fx%.0f points %.2f Hz %.1fs\n",
            width,height,bounds.size.width,bounds.size.height,refresh,seconds);
    fflush(stderr);
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,(int64_t)(seconds*NSEC_PER_SEC)),dispatch_get_main_queue(),^{
        if(CGDisplayStreamStop(stream)){fprintf(stderr,"display stream could not stop\n");exit(1);}
    });
    dispatch_main();
}

int main(int argc,char **argv)
{
    @autoreleasepool {
        if(argc!=3){fprintf(stderr,"usage: %s NEW.mov|--timestamps-only SECONDS_1_TO_30\n",argv[0]);return 2;}
        char *end=NULL;double seconds=strtod(argv[2],&end);
        if(!end||end==argv[2]||*end||!isfinite(seconds)||seconds<1||seconds>30)return 2;
        if(!strcmp(argv[1],"--timestamps-only"))return recordTiming(seconds);
        if(!access(argv[1],F_OK))return 2;
        CGDirectDisplayID display=CGMainDisplayID();
        size_t width=1024,physicalWidth=CGDisplayPixelsWide(display),physicalHeight=CGDisplayPixelsHigh(display);
        if(!physicalWidth||!physicalHeight)return 2;
        size_t height=((width*physicalHeight/physicalWidth)+1)&~(size_t)1;
        if(height<200||height>4096)return 2;
        NSURL *url=[NSURL fileURLWithPath:@(argv[1])];NSError *error=nil;
        AVAssetWriter *writer=[[AVAssetWriter alloc] initWithURL:url fileType:AVFileTypeQuickTimeMovie error:&error];
        NSDictionary *settings=@{AVVideoCodecKey:AVVideoCodecH264,AVVideoWidthKey:@(width),AVVideoHeightKey:@(height),
            AVVideoCompressionPropertiesKey:@{AVVideoAverageBitRateKey:@8000000}};
        AVAssetWriterInput *input=[AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo outputSettings:settings];
        input.expectsMediaDataInRealTime=YES;
        AVAssetWriterInputPixelBufferAdaptor *adaptor=[AVAssetWriterInputPixelBufferAdaptor assetWriterInputPixelBufferAdaptorWithAssetWriterInput:input sourcePixelBufferAttributes:nil];
        if(!writer||![writer canAddInput:input]){fprintf(stderr,"writer configuration: %s\n",error.description.UTF8String);return 1;}
        [writer addInput:input];
        if(![writer startWriting]){fprintf(stderr,"writer start: %s\n",writer.error.description.UTF8String);return 1;}
        [writer startSessionAtSourceTime:kCMTimeZero];
        mach_timebase_info_data_t timebase;mach_timebase_info(&timebase);
        dispatch_queue_t queue=dispatch_queue_create("org.vim.research.native-effect-capture",DISPATCH_QUEUE_SERIAL);
        __block uint64_t firstTime=0,frames=0,skipped=0,failed=0;
        CGDisplayStreamRef stream=CGDisplayStreamCreateWithDispatchQueue(display,width,height,'BGRA',
            (__bridge CFDictionaryRef)@{(__bridge NSString *)kCGDisplayStreamShowCursor:@YES,
                (__bridge NSString *)kCGDisplayStreamMinimumFrameTime:@(1.0/60.0)},queue,
            ^(CGDisplayStreamFrameStatus status,uint64_t time,IOSurfaceRef surface,CGDisplayStreamUpdateRef update){
                (void)update;if(status!=kCGDisplayStreamFrameStatusFrameComplete||!surface)return;
                @autoreleasepool {
                    if(!input.readyForMoreMediaData){++skipped;return;}
                    CVPixelBufferRef buffer=NULL;
                    CVReturn result=CVPixelBufferCreateWithIOSurface(kCFAllocatorDefault,surface,NULL,&buffer);
                    if(result||!buffer){++failed;return;}
                    if(!firstTime)firstTime=time;
                    double nanos=(double)(time-firstTime)*timebase.numer/timebase.denom;
                    if(![adaptor appendPixelBuffer:buffer withPresentationTime:CMTimeMake((int64_t)nanos,1000000000)])++failed;
                    else ++frames;
                    CVPixelBufferRelease(buffer);
                }
            });
        if(!stream||CGDisplayStreamStart(stream)){fprintf(stderr,"display stream could not start\n");if(stream)CFRelease(stream);[writer cancelWriting];return 1;}
        fprintf(stderr,"READY native effect stream %zux%zu %.1fs\n",width,height,seconds);fflush(stderr);
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,(int64_t)(seconds*NSEC_PER_SEC)),dispatch_get_main_queue(),^{
            CGDisplayStreamStop(stream);
            dispatch_async(queue,^{
                [input markAsFinished];
                [writer finishWritingWithCompletionHandler:^{
                    fprintf(stderr,"native effect frames=%llu skipped=%llu failed=%llu writer=%ld error=%s\n",
                        (unsigned long long)frames,(unsigned long long)skipped,(unsigned long long)failed,
                        (long)writer.status,writer.error?writer.error.description.UTF8String:"none");
                    CFRelease(stream);exit(writer.status==AVAssetWriterStatusCompleted&&frames&&!failed?0:1);
                }];
            });
        });
        dispatch_main();
    }
}
