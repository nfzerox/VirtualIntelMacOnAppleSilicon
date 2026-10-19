#import <AppKit/AppKit.h>
// Export standard 64px ICNS slices, or downsample the supplied raster artwork.
int main(int argc,const char **argv) { @autoreleasepool {
    if(argc!=6)return 2;
    NSArray *names=@[@"RecommendedUTM",@"RecommendedVirtualBuddy",@"RecommendedParallels",@"RecommendedVirtualMac"];
    for(NSUInteger i=0;i<names.count;i++) {
        NSImage *image=[[NSImage alloc] initWithContentsOfFile:@(argv[i+2])];if(!image)return 1;
        NSBitmapImageRep *bitmap=nil;
        for(NSImageRep *rep in image.representations)
            if([rep isKindOfClass:NSBitmapImageRep.class]&&rep.pixelsWide==64&&rep.pixelsHigh==64){bitmap=(id)rep;break;}
        if(!bitmap) {
            bitmap=[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:64 pixelsHigh:64 bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSCalibratedRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
            [NSGraphicsContext saveGraphicsState];[NSGraphicsContext setCurrentContext:[NSGraphicsContext graphicsContextWithBitmapImageRep:bitmap]];
            [image drawInRect:NSMakeRect(0,0,64,64) fromRect:NSZeroRect operation:NSCompositingOperationCopy fraction:1];
            [NSGraphicsContext restoreGraphicsState];
        }
        NSString *path=[@(argv[1]) stringByAppendingPathComponent:[names[i] stringByAppendingString:@".imageset/icon.png"]];
        if(![[bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:path atomically:YES])return 1;
    }
    return 0;
} }
