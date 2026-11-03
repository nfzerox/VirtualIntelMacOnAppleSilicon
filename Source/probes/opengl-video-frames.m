/* Offline inspection of a bounded host-side VM-window recording. No guest
 * screen capture or live per-frame readback. This is visual correctness
 * evidence, not exact-pixel conformance or an uninstrumented FPS measurement.
 */
#import <Cocoa/Cocoa.h>
#import <AVFoundation/AVFoundation.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <sys/stat.h>
int main(int argc,char **argv)
{
    if(argc!=3&&argc!=5&&argc!=6){fprintf(stderr,"usage: %s MOVIE NEW_OUTPUT_DIRECTORY [START_SECONDS END_SECONDS [FPS_1_TO_20]]\n",argv[0]);return 2;}
    @autoreleasepool {
        AVURLAsset *asset=[AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:@(argv[1])] options:nil];
        double duration=CMTimeGetSeconds(asset.duration);
        if(!isfinite(duration)||duration<=0||duration>30){fputs("Expected a recording of at most 30 seconds\n",stderr);return 2;}
        double first=0,last=duration;
        if(argc>=5){
            char *a=NULL,*b=NULL;first=strtod(argv[3],&a);last=strtod(argv[4],&b);
            if(!a||*a||!b||*b||!isfinite(first)||!isfinite(last)||first<0||last<=first||last>duration){
                fputs("Choose a finite increasing range within the movie\n",stderr);return 2;
            }
        }
        unsigned fps=20;
        if(argc==6){
            char *end=NULL;errno=0;unsigned long value=strtoul(argv[5],&end,10);
            if(errno||!end||end==argv[5]||*end||value<1||value>20){fputs("Choose an integer sampling rate from 1 to 20\n",stderr);return 2;}
            fps=(unsigned)value;
        }
        if(mkdir(argv[2],0700)){perror("new output directory");return 2;}
        AVAssetImageGenerator *generator=[[AVAssetImageGenerator alloc] initWithAsset:asset];
        generator.appliesPreferredTrackTransform=YES;
        generator.maximumSize=CGSizeMake(1024,768);
        generator.requestedTimeToleranceBefore=kCMTimeZero;
        generator.requestedTimeToleranceAfter=kCMTimeZero;
        unsigned count=(unsigned)ceil(last*fps);
        for(unsigned i=(unsigned)ceil(first*fps);i<count;++i){@autoreleasepool {
            NSError *error=nil;CMTime actual=kCMTimeZero;
            CGImageRef cg=[generator copyCGImageAtTime:CMTimeMake(i,fps) actualTime:&actual error:&error];
            if(!cg){fprintf(stderr,"frame %u: %s\n",i,error.localizedDescription.UTF8String);return 1;}
            NSBitmapImageRep *image=[[NSBitmapImageRep alloc] initWithCGImage:cg];CGImageRelease(cg);
            NSData *png=[image representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
            NSString *path=[@(argv[2]) stringByAppendingPathComponent:[NSString stringWithFormat:@"frame-%03u.png",i]];
            if(!png||![png writeToFile:path options:NSDataWritingWithoutOverwriting error:&error])return 1;
            printf("%u requested=%.3f actual=%.6f\n",i,i/(double)fps,CMTimeGetSeconds(actual));
        }}
    }return 0;
}
