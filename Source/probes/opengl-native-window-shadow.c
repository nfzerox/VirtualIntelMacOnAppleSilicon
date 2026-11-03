/* Read-only, real application window framing/alpha comparison. No own window,
 * desktop capture files, permissions changes or private compositor offsets. */
#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv)
{
    if(argc!=2)return 64;
    CFArrayRef list=CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly,kCGNullWindowID);if(!list)return 1;
    CGWindowID window=0;
    for(CFIndex i=0;i<CFArrayGetCount(list);i++){
        CFDictionaryRef row=CFArrayGetValueAtIndex(list,i);char owner[256]={0};int layer=-1;
        CFStringRef name=CFDictionaryGetValue(row,kCGWindowOwnerName);
        if(name)CFStringGetCString(name,owner,sizeof(owner),kCFStringEncodingUTF8);
        CFNumberGetValue(CFDictionaryGetValue(row,kCGWindowLayer),kCFNumberIntType,&layer);
        if(!strcmp(owner,argv[1])&&!layer){CFNumberGetValue(CFDictionaryGetValue(row,kCGWindowNumber),kCFNumberIntType,&window);break;}
    }
    CFRelease(list);if(!window){puts("No matching on-screen application window");return 2;}
    CGWindowImageOption resolution=kCGWindowImageNominalResolution;
    CGImageRef full=CGWindowListCreateImage(CGRectNull,kCGWindowListOptionIncludingWindow,window,resolution);
    CGImageRef plain=CGWindowListCreateImage(CGRectNull,kCGWindowListOptionIncludingWindow,window,resolution|kCGWindowImageBoundsIgnoreFraming);
    if(!full||!plain)return 3;
    size_t width=CGImageGetWidth(full),height=CGImageGetHeight(full),inner=CGImageGetWidth(plain);
    if(!width||!height||width>8192||height>8192)return 4;
    unsigned char *pixels=calloc(height,width*4);CGColorSpaceRef space=CGColorSpaceCreateDeviceRGB();
    CGContextRef context=CGBitmapContextCreate(pixels,width,height,8,width*4,space,kCGImageAlphaPremultipliedLast|kCGBitmapByteOrder32Big);
    if(!pixels||!context)return 5;
    CGContextDrawImage(context,CGRectMake(0,0,width,height),full);
    size_t margin=width>inner?(width-inner)/2:0;unsigned nonzero=0,peak=0;unsigned long long alpha=0;
    for(size_t y=height/4;y<height*3/4;y++)for(size_t x=0;x<margin;x++){
        unsigned a=pixels[(y*width+x)*4+3];alpha+=a;nonzero+=a!=0;if(a>peak)peak=a;
    }
    printf("app=%s full=%zux%zu unframed=%zux%zu side_margin=%zu halo_nonzero=%u halo_alpha=%llu halo_peak=%u\n",
        argv[1],width,height,inner,CGImageGetHeight(plain),margin,nonzero,alpha,peak);
    CGContextRelease(context);CGColorSpaceRelease(space);free(pixels);CGImageRelease(full);CGImageRelease(plain);return 0;
}
