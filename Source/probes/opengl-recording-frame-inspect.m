// Exact-size, explicit recording inspection. AVFoundation only; no live
// screen capture, input, guest connection, audio extraction or production use.
#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <ImageIO/ImageIO.h>
#include <math.h>
int main(int argc,char **argv) {@autoreleasepool {
    if(argc!=6)return 64;
    NSURL *input=[NSURL fileURLWithPath:@(argv[1])],*output=[NSURL fileURLWithPath:@(argv[2]) isDirectory:YES];
    char *end=NULL;double start=strtod(argv[3],&end);if(!*argv[3]||*end||!isfinite(start)||start<0)return 64;
    double step=strtod(argv[4],&end);if(!*argv[4]||*end||!isfinite(step)||step<1./240)return 64;
    long count=strtol(argv[5],&end,10);if(!*argv[5]||*end||count<1||count>240)return 64;
    AVURLAsset *asset=[AVURLAsset URLAssetWithURL:input options:nil];
    double duration=CMTimeGetSeconds(asset.duration);
    if(!isfinite(duration)||start+(count-1)*step>=duration)return 65;
    if([NSFileManager.defaultManager fileExistsAtPath:output.path])return 73;
    NSError *error=nil;
    if(![NSFileManager.defaultManager createDirectoryAtURL:output withIntermediateDirectories:NO attributes:@{NSFilePosixPermissions:@0700} error:&error]){NSLog(@"%@",error);return 73;}
    AVAssetImageGenerator *generator=[[AVAssetImageGenerator alloc] initWithAsset:asset];
    generator.appliesPreferredTrackTransform=YES;
    generator.requestedTimeToleranceBefore=kCMTimeZero;generator.requestedTimeToleranceAfter=kCMTimeZero;
    for(long i=0;i<count;i++){@autoreleasepool{
        CMTime actual=kCMTimeInvalid;
        CGImageRef image=[generator copyCGImageAtTime:CMTimeMakeWithSeconds(start+i*step,60000) actualTime:&actual error:&error];
        if(!image){NSLog(@"%@",error);return 1;}
        NSURL *file=[output URLByAppendingPathComponent:[NSString stringWithFormat:@"frame-%03ld.png",i]];
        CGImageDestinationRef destination=CGImageDestinationCreateWithURL((__bridge CFURLRef)file,CFSTR("public.png"),1,NULL);
        if(destination)CGImageDestinationAddImage(destination,image,NULL);
        BOOL okay=destination&&CGImageDestinationFinalize(destination);
        printf("%ld requested=%.6f actual=%.6f size=%zux%zu\n",i,start+i*step,CMTimeGetSeconds(actual),CGImageGetWidth(image),CGImageGetHeight(image));
        if(destination)CFRelease(destination);CGImageRelease(image);if(!okay)return 1;
    }}
    return 0;
}}
