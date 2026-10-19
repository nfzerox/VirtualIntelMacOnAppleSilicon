// Deterministic image-import fixture, generated without CI/GL/video decoding.
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
int main(int argc,const char **argv) {@autoreleasepool {
    if(argc!=2)return 64;
    const size_t w=512,h=384;
    NSMutableData *data=[NSMutableData dataWithLength:w*h*4];uint8_t *p=data.mutableBytes;
    for(size_t y=0;y<h;y++)for(size_t x=0;x<w;x++) {
        size_t i=(y*w+x)*4;p[i]=(uint8_t)(x/2);p[i+1]=(uint8_t)(y*255/(h-1));
        p[i+2]=((x/32+y/32)%2)?240:16;p[i+3]=255;
    }
    CGDataProviderRef provider=CGDataProviderCreateWithCFData((__bridge CFDataRef)data);
    CGColorSpaceRef space=CGColorSpaceCreateDeviceRGB();
    CGImageRef image=CGImageCreate(w,h,8,32,w*4,space,kCGImageAlphaLast,provider,NULL,NO,kCGRenderingIntentDefault);
    NSURL *url=[NSURL fileURLWithPath:@(argv[1])];
    CGImageDestinationRef dest=CGImageDestinationCreateWithURL((__bridge CFURLRef)url,CFSTR("public.png"),1,NULL);
    if(!image||!dest)return 1;
    CGImageDestinationAddImage(dest,image,NULL);BOOL ok=CGImageDestinationFinalize(dest);
    CFRelease(dest);CGImageRelease(image);CGColorSpaceRelease(space);CGDataProviderRelease(provider);
    return ok?0:1;
}}
