/* Finite QTKit decode/output diagnostic. Request native CPU pixel-buffer and
 * CGImage outputs without creating a CGL context or displaying a movie view.
 * This does not assert that QTKit's private internals never choose a GPU path. */
#import <Cocoa/Cocoa.h>
#import <QTKit/QTKit.h>
#import <CoreVideo/CoreVideo.h>
#import <ApplicationServices/ApplicationServices.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
static int Error(const char *stage,NSError *error){
    fprintf(stderr,"%s failed: domain=%s code=%ld\n",stage,error?[[error domain] UTF8String]:"none",(long)[error code]);return 1;
}
int main(int argc,char **argv){
    if(argc<3||argc>4){fprintf(stderr,"usage: %s MOVIE NEW_OUTPUT_DIRECTORY [SECONDS=14]\n",argv[0]);return 64;}
    char *end=NULL;double second=argc==4?strtod(argv[3],&end):14;
    if(!isfinite(second)||second<0||second>3600||(argc==4&&(!end||*end)))return 64;
    // Diagnostic only, never an installer, decoder or application policy.
    alarm(90);setvbuf(stdout,NULL,_IONBF,0);
    NSAutoreleasePool *pool=[[NSAutoreleasePool alloc] init];
    if(mkdir(argv[2],0700)){perror("new output directory");return 1;}
    NSError *error=nil;CFAbsoluteTime start=CFAbsoluteTimeGetCurrent();
    NSDictionary *options=[NSDictionary dictionaryWithObjectsAndKeys:
        [NSURL fileURLWithPath:[NSString stringWithUTF8String:argv[1]]],QTMovieURLAttribute,
        [NSNumber numberWithBool:YES],QTMovieDontInteractWithUserAttribute,
        [NSNumber numberWithBool:NO],QTMovieOpenAsyncOKAttribute,
        [NSNumber numberWithBool:YES],QTMovieOpenForPlaybackAttribute,nil];
    puts("opening-native-movie");
    QTMovie *movie=[[[QTMovie alloc] initWithAttributes:options error:&error] autorelease];
    if(!movie)return Error("movie open",error);
    printf("opened seconds=%.3f requested-frame=%.3f\n",CFAbsoluteTimeGetCurrent()-start,second);
    QTTime time=QTMakeTime((long long)llround(second*600),600);
    NSDictionary *attributes=[NSDictionary dictionaryWithObject:QTMovieFrameImageTypeCVPixelBufferRef forKey:QTMovieFrameImageType];
    puts("requesting-cpu-pixel-buffer");error=nil;
    CVPixelBufferRef pixels=(CVPixelBufferRef)[movie frameImageAtTime:time withAttributes:attributes error:&error];
    if(pixels){
        OSType format=CVPixelBufferGetPixelFormatType(pixels);
        printf("pixel-buffer=%zux%zu format=%08x planes=%zu rowbytes=%zu\n",CVPixelBufferGetWidth(pixels),
            CVPixelBufferGetHeight(pixels),(unsigned)format,CVPixelBufferGetPlaneCount(pixels),CVPixelBufferGetBytesPerRow(pixels));
    }else Error("pixel-buffer output",error);
    attributes=[NSDictionary dictionaryWithObject:QTMovieFrameImageTypeCGImageRef forKey:QTMovieFrameImageType];
    puts("requesting-cpu-cgimage");error=nil;
    CGImageRef image=(CGImageRef)[movie frameImageAtTime:time withAttributes:attributes error:&error];
    if(!image)return Error("CGImage output",error);
    size_t width=CGImageGetWidth(image),height=CGImageGetHeight(image);
    if(!width||!height||width>8192||height>8192)return 1;
    unsigned char *rgba=calloc(width*height,4);CGColorSpaceRef color=CGColorSpaceCreateDeviceRGB();
    CGContextRef bitmap=rgba&&color?CGBitmapContextCreate(rgba,width,height,8,width*4,color,
        kCGImageAlphaPremultipliedLast|kCGBitmapByteOrder32Big):NULL;
    if(!bitmap){free(rgba);if(color)CGColorSpaceRelease(color);return 1;}
    CGContextDrawImage(bitmap,CGRectMake(0,0,width,height),image);
    uint64_t sum=0;size_t nonblack=0;unsigned minimum=255,maximum=0;
    for(size_t i=0;i<width*height;i++){
        unsigned char *p=rgba+i*4;nonblack+=p[0]>8||p[1]>8||p[2]>8;
        for(unsigned c=0;c<3;c++){sum+=p[c];if(p[c]<minimum)minimum=p[c];if(p[c]>maximum)maximum=p[c];}
    }
    printf("cgimage=%zux%zu nonblack=%zu/%zu rgb-range=%u..%u rgb-mean=%.3f elapsed=%.3f\n",
        width,height,nonblack,width*height,minimum,maximum,(double)sum/(width*height*3),CFAbsoluteTimeGetCurrent()-start);
    CGImageRef output=CGBitmapContextCreateImage(bitmap);
    NSURL *url=[NSURL fileURLWithPath:[[NSString stringWithUTF8String:argv[2]] stringByAppendingPathComponent:@"frame.png"]];
    CGImageDestinationRef png=output?CGImageDestinationCreateWithURL((CFURLRef)url,CFSTR("public.png"),1,NULL):NULL;
    if(png)CGImageDestinationAddImage(png,output,NULL);
    int result=png&&CGImageDestinationFinalize(png)?0:1;
    if(png)CFRelease(png);if(output)CGImageRelease(output);CGContextRelease(bitmap);CGColorSpaceRelease(color);free(rgba);
    [pool drain];return result;
}
