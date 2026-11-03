/* Diagnostic only: inspect a specific WindowServer backing image, rather
 * than assuming that a black scanout means applications painted black. */
#include <ApplicationServices/ApplicationServices.h>
#include <ImageIO/ImageIO.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv)
{
    if(argc!=3) { fprintf(stderr,"usage: %s window-id output.png\n",argv[0]);return 2; }
    CGWindowID window=(CGWindowID)strtoul(argv[1],NULL,0);
    CGImageRef image=CGWindowListCreateImage(CGRectNull,kCGWindowListOptionIncludingWindow,window,kCGWindowImageBoundsIgnoreFraming);
    if(!image) { fputs("No backing image\n",stderr);return 1; }
    CFDataRef bytes=CGDataProviderCopyData(CGImageGetDataProvider(image));
    const unsigned char *p=bytes?CFDataGetBytePtr(bytes):NULL;
    size_t n=bytes?(size_t)CFDataGetLength(bytes):0,nonzero=0;
    for(size_t i=0;i<n;++i) nonzero+=p[i]!=0;
    printf("window=%u size=%zux%zu row=%zu bits=%zu/%zu bitmap=%x nonzero=%zu/%zu\n",window,
        CGImageGetWidth(image),CGImageGetHeight(image),CGImageGetBytesPerRow(image),CGImageGetBitsPerComponent(image),
        CGImageGetBitsPerPixel(image),(unsigned)CGImageGetBitmapInfo(image),nonzero,n);
    CFURLRef url=CFURLCreateFromFileSystemRepresentation(NULL,(const UInt8 *)argv[2],strlen(argv[2]),false);
    CGImageDestinationRef out=url?CGImageDestinationCreateWithURL(url,CFSTR("public.png"),1,NULL):NULL;
    if(out) CGImageDestinationAddImage(out,image,NULL);
    int okay=out&&CGImageDestinationFinalize(out);
    if(out) CFRelease(out);if(url) CFRelease(url);if(bytes) CFRelease(bytes);CGImageRelease(image);
    return okay?0:1;
}
