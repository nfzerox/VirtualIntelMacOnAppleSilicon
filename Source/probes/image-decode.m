#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <CoreGraphics/CoreGraphics.h>
#include <unistd.h>
int main(int argc, char **argv) {
    @autoreleasepool {
        alarm(20);
        if (argc != 2) return 2;
        NSURL *url = [NSURL fileURLWithPath:@(argv[1])];
        CGImageSourceRef source = CGImageSourceCreateWithURL((CFURLRef)url, NULL);
        if (!source) return 3;
        fprintf(stderr, "source count=%zu\n", CGImageSourceGetCount(source));
        CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, NULL);
        if (!image) return 4;
        fprintf(stderr, "image %zux%zu; decoding\n", CGImageGetWidth(image), CGImageGetHeight(image));
        CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
        CGContextRef ctx = CGBitmapContextCreate(NULL, 256, 256, 8, 1024, cs, (CGBitmapInfo)kCGImageAlphaPremultipliedLast);
        if (!ctx) return 5;
        CGContextDrawImage(ctx, CGRectMake(0, 0, 256, 256), image);
        unsigned char *bytes = CGBitmapContextGetData(ctx);
        unsigned long long sum = 0;
        for (size_t i = 0; i < 256*1024; i++) sum += bytes[i];
        fprintf(stderr, "PASS decoded checksum=%llu\n", sum);
        CGContextRelease(ctx); CGColorSpaceRelease(cs);
        CGImageRelease(image); CFRelease(source);
        return 0;
    }
}
