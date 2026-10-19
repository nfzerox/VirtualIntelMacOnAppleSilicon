// Read-only guest diagnostic: solid pixels need neither image nor video decode.
// Build x86_64, deployment 10.14, Foundation/CoreImage/CoreGraphics/Metal.
#import <Foundation/Foundation.h>
#import <CoreImage/CoreImage.h>
#import <Metal/Metal.h>
#include <mach-o/dyld.h>
#include <unistd.h>

int main(int argc, const char **argv) {@autoreleasepool {
    alarm(30);
    if(argc!=2||strcmp(argv[1],"cpu")&&strcmp(argv[1],"metal"))return 64;
    BOOL cpu=!strcmp(argv[1],"cpu");
    CIContext *context=cpu?[CIContext contextWithOptions:@{kCIContextUseSoftwareRenderer:@YES}]:
        [CIContext contextWithMTLDevice:MTLCreateSystemDefaultDevice()];
    NSLog(@"context=%@",context);
    if(!context)return 1;
    CIImage *image=[[CIImage imageWithColor:[CIColor colorWithRed:1 green:0.5 blue:0.25 alpha:1]]
        imageByCroppingToRect:CGRectMake(0,0,32,32)];
    unsigned char pixels[32*32*4];memset(pixels,0xa5,sizeof(pixels));
    CGColorSpaceRef space=CGColorSpaceCreateDeviceRGB();
    [context render:image toBitmap:pixels rowBytes:128 bounds:image.extent format:kCIFormatRGBA8 colorSpace:space];
    uint64_t sum=0;for(size_t i=0;i<sizeof(pixels);i++)sum+=pixels[i];
    printf("direct sum=%llu first=%u,%u,%u,%u\n",sum,pixels[0],pixels[1],pixels[2],pixels[3]);
    memset(pixels,0xa5,sizeof(pixels));
    CIRenderDestination *destination=[[CIRenderDestination alloc] initWithBitmapData:pixels
        width:32 height:32 bytesPerRow:128 format:kCIFormatRGBA8];
    destination.colorSpace=space;
    NSError *error=nil;
    CIRenderTask *task=[context startTaskToRender:image toDestination:destination error:&error];
    CIRenderInfo *info=[task waitUntilCompletedAndReturnError:&error];
    NSLog(@"task=%@ info=%@ error=%@",task,info,error);
    sum=0;for(size_t i=0;i<sizeof(pixels);i++)sum+=pixels[i];
    printf("task sum=%llu first=%u,%u,%u,%u\n",sum,pixels[0],pixels[1],pixels[2],pixels[3]);
    for(uint32_t i=0;i<_dyld_image_count();i++) {
        const char *name=_dyld_get_image_name(i);
        if(strstr(name,"CoreImage")||strstr(name,"libGI.")||strstr(name,"LLVM")||
           strstr(name,"OpenGL")||strstr(name,"GLRenderer"))printf("image=%s\n",name);
    }
    CGColorSpaceRelease(space);
    return !info||error?2:0;
}}
