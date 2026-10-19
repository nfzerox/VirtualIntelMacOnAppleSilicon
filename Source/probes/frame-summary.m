// Read-only pixel summary for an app-owned captured frame; no desktop capture.
#import <Cocoa/Cocoa.h>
int main(int argc,const char **argv) { @autoreleasepool {
    if(argc!=2)return 64;
    NSBitmapImageRep *input=[[NSBitmapImageRep alloc] initWithData:[NSData dataWithContentsOfFile:@(argv[1])]];
    CGImageRef image=input.CGImage;if(!image)return 65;
    size_t width=CGImageGetWidth(image),height=CGImageGetHeight(image);
    if(!width||!height||width>8192||height>8192)return 65;
    CGColorSpaceRef color=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef context=CGBitmapContextCreate(NULL,width,height,8,width*4,color,kCGImageAlphaNoneSkipLast|kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(color);if(!context)return 70;
    CGContextDrawImage(context,CGRectMake(0,0,width,height),image);
    const uint8_t *pixels=CGBitmapContextGetData(context);uint64_t sum=0,nonblack=0;
    for(size_t i=0;i<width*height;i++) {
        unsigned rgb=pixels[i*4]+pixels[i*4+1]+pixels[i*4+2];
        sum+=rgb;if(rgb)nonblack++;
    }
    NSDictionary *summary=@{@"Width":@(width),@"Height":@(height),@"NonblackPixels":@(nonblack),
        @"AverageRGB":@((double)sum/(width*height*3)),@"AllBlack":nonblack==0?@YES:@NO};
    NSData *json=[NSJSONSerialization dataWithJSONObject:summary options:NSJSONWritingSortedKeys error:NULL];
    fwrite(json.bytes,1,json.length,stdout);fputc('\n',stdout);CGContextRelease(context);return 0;
} }
