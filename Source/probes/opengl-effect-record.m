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
#include <unistd.h>
int main(int argc,char **argv)
{
    @autoreleasepool {
        if(argc!=3){fprintf(stderr,"usage: %s NEW.mov SECONDS_1_TO_30\n",argv[0]);return 2;}
        char *end=NULL;double seconds=strtod(argv[2],&end);
        if(!end||end==argv[2]||*end||!isfinite(seconds)||seconds<1||seconds>30||!access(argv[1],F_OK))return 2;
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
