// Deterministic developer-only asset exporter; not compiled into the app.
// ImageIO handles individual HEIC frames without decoding the whole collection.
#import <Cocoa/Cocoa.h>
#import <ImageIO/ImageIO.h>

static void SVGPath(void *info,const CGPathElement *element) {
    NSMutableString *s=(__bridge NSMutableString *)info;CGPoint *p=element->points;
    switch(element->type) {
        case kCGPathElementMoveToPoint:[s appendFormat:@"M%.5f %.5f",p[0].x,p[0].y];break;
        case kCGPathElementAddLineToPoint:[s appendFormat:@"L%.5f %.5f",p[0].x,p[0].y];break;
        case kCGPathElementAddQuadCurveToPoint:[s appendFormat:@"Q%.5f %.5f %.5f %.5f",p[0].x,p[0].y,p[1].x,p[1].y];break;
        case kCGPathElementAddCurveToPoint:[s appendFormat:@"C%.5f %.5f %.5f %.5f %.5f %.5f",p[0].x,p[0].y,p[1].x,p[1].y,p[2].x,p[2].y];break;
        case kCGPathElementCloseSubpath:[s appendString:@"Z"];break;
    }
}
int main(int argc,const char **argv) {@autoreleasepool {
    if(argc==5&&strcmp(argv[1],"wallpaper")==0) {
        NSURL *source=[NSURL fileURLWithPath:@(argv[2])],*output=[NSURL fileURLWithPath:@(argv[4])];
        CGImageSourceRef images=CGImageSourceCreateWithURL((__bridge CFURLRef)source,NULL);
        size_t index=strtoul(argv[3],NULL,10);
        if(!images||index>=CGImageSourceGetCount(images)){fprintf(stderr,"Missing HEIC image %zu\n",index);return 1;}
        CGImageRef image=CGImageSourceCreateThumbnailAtIndex(images,index,(__bridge CFDictionaryRef)@{
            (id)kCGImageSourceCreateThumbnailFromImageAlways:@YES,(id)kCGImageSourceCreateThumbnailWithTransform:@YES,(id)kCGImageSourceThumbnailMaxPixelSize:@1280});
        CGImageDestinationRef dest=CGImageDestinationCreateWithURL((__bridge CFURLRef)output,CFSTR("public.jpeg"),1,NULL);
        if(!image||!dest)return 1;
        CGImageDestinationAddImage(dest,image,(__bridge CFDictionaryRef)@{(id)kCGImageDestinationLossyCompressionQuality:@.82});
        BOOL ok=CGImageDestinationFinalize(dest);CFRelease(dest);CGImageRelease(image);CFRelease(images);return ok?0:1;
    }
    if(argc==3&&strcmp(argv[1],"symbol")==0) {
        [NSApplication sharedApplication];
        NSImageSymbolConfiguration *configuration=[[NSImageSymbolConfiguration configurationWithPointSize:256 weight:NSFontWeightMedium] configurationByApplyingConfiguration:NSImageSymbolConfiguration.configurationPreferringMonochrome];
        NSImage *symbol=[[NSImage imageWithSystemSymbolName:@"laptopcomputer" accessibilityDescription:nil] imageWithSymbolConfiguration:configuration];
        // Developer-time export only: AppKit's symbol rep exposes the original
        // Bezier outline. This avoids PDF export rasterizing RenderBox symbols.
        // Neither this private accessor nor any font dependency ships in app code.
        NSImageRep *rep=symbol.representations.firstObject;
        if(![rep respondsToSelector:NSSelectorFromString(@"outlinePath")])return 1;
        NSBezierPath *bezier=[rep valueForKey:@"outlinePath"];
        if(![bezier isKindOfClass:NSBezierPath.class]||bezier.empty)return 1;
        CGRect bounds=bezier.bounds;NSMutableString *outline=[NSMutableString new];CGPathApply(bezier.CGPath,(__bridge void *)outline,SVGPath);
        NSURL *directory=[NSURL fileURLWithPath:@(argv[2]) isDirectory:YES];
        [NSFileManager.defaultManager createDirectoryAtURL:directory withIntermediateDirectories:YES attributes:nil error:nil];
        // Locate the actual screen opening (the second closed contour). Its
        // center, rather than the whole laptop including its base, is where
        // the next nested Mac belongs. This also survives symbol revisions.
        __block NSUInteger contour=0;__block CGRect screen=CGRectNull;
        __block CGMutablePathRef part=CGPathCreateMutable();
        CGPathApplyWithBlock(bezier.CGPath,^(const CGPathElement *element){CGPoint *p=element->points;
            switch(element->type) {
                case kCGPathElementMoveToPoint:CGPathMoveToPoint(part,NULL,p[0].x,p[0].y);break;
                case kCGPathElementAddLineToPoint:CGPathAddLineToPoint(part,NULL,p[0].x,p[0].y);break;
                case kCGPathElementAddQuadCurveToPoint:CGPathAddQuadCurveToPoint(part,NULL,p[0].x,p[0].y,p[1].x,p[1].y);break;
                case kCGPathElementAddCurveToPoint:CGPathAddCurveToPoint(part,NULL,p[0].x,p[0].y,p[1].x,p[1].y,p[2].x,p[2].y);break;
                case kCGPathElementCloseSubpath:CGPathCloseSubpath(part);if(++contour==2)screen=CGPathGetPathBoundingBox(part);CGPathRelease(part);part=CGPathCreateMutable();break;
            }
        });CGPathRelease(part);if(CGRectIsNull(screen))return 1;
        // Optical center: the wide base makes a geometrically centered laptop
        // read low. Move the complete nested group up together.
        double widths[]={832,468,216};double centers[3]={512,0,0};
        for(int i=1;i<3;i++)centers[i]=centers[i-1]+(CGRectGetMidY(screen)-CGRectGetMidY(bounds))*widths[i-1]/bounds.size.width;
        for(int i=0;i<3;i++) {
            double scale=widths[i]/bounds.size.width;
            NSString *svg=[NSString stringWithFormat:@"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1024\" height=\"1024\" viewBox=\"0 0 1024 1024\"><g transform=\"translate(%.5f %.5f) scale(%.6f %.6f) translate(%.5f %.5f)\"><path fill=\"white\" d=\"%@\"/></g></svg>\n",512-widths[i]/2,centers[i]-bounds.size.height*scale/2,scale,scale,-bounds.origin.x,-bounds.origin.y,outline];
            if(![svg writeToURL:[directory URLByAppendingPathComponent:[NSString stringWithFormat:@"laptop-%d.svg",i+1]] atomically:YES encoding:NSUTF8StringEncoding error:nil])return 1;
        }
        return 0;
    }
    fprintf(stderr,"Usage: export-app-artwork wallpaper input.heic zero-based-index output.jpg\n       export-app-artwork symbol output-directory\n");return 64;
}}
