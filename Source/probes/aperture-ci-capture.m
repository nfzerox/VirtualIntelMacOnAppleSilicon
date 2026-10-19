// Explicit LLDB-only diagnostic. No constructor, interposition, or installation.
#import <Foundation/Foundation.h>
#import <CoreImage/CoreImage.h>
#import <Metal/Metal.h>
#import <ImageIO/ImageIO.h>
#import <OpenGL/OpenGL.h>
void VIMCaptureCIImage(CIImage *source) {
    // Run after the debugger detaches so provider worker threads can execute.
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0), ^{
        @autoreleasepool {
            CGRect extent=source.extent;
            if(CGRectIsEmpty(extent)||CGRectIsInfinite(extent))return;
            CIImage *image=[source imageByApplyingTransform:CGAffineTransformMakeScale(
                512/extent.size.width,512/extent.size.height)];
            for(NSString *mode in @[@"metal",@"cpu",@"gl"]) {
            CGLContextObj gl=NULL;CGLPixelFormatObj pixelFormat=NULL;GLint count=0;
            CIContext *context=nil;
            CGColorSpaceRef space=CGColorSpaceCreateDeviceRGB();
            if([mode isEqual:@"gl"]) {
                CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,0};
                if(CGLChoosePixelFormat(attrs,&pixelFormat,&count)||!pixelFormat||
                   CGLCreateContext(pixelFormat,NULL,&gl)||CGLSetCurrentContext(gl))continue;
                context=[CIContext contextWithCGLContext:gl pixelFormat:pixelFormat colorSpace:space options:nil];
            } else if([mode isEqual:@"cpu"])context=[CIContext contextWithOptions:@{kCIContextUseSoftwareRenderer:@YES}];
            else context=[CIContext contextWithMTLDevice:MTLCreateSystemDefaultDevice()];
            CGImageRef bitmap=[context createCGImage:image fromRect:image.extent
                format:kCIFormatRGBA8 colorSpace:space];
            CGColorSpaceRelease(space);
            // Only this explicitly invoked diagnostic writes this fixed output.
            if(bitmap) {
                NSURL *url=[NSURL fileURLWithPath:[NSHomeDirectory() stringByAppendingPathComponent:[NSString stringWithFormat:@"pvg-aperture-probes/ci-current-%@.png",mode]]];
                CGImageDestinationRef output=CGImageDestinationCreateWithURL((CFURLRef)url,CFSTR("public.png"),1,NULL);
                if(output){CGImageDestinationAddImage(output,bitmap,NULL);CGImageDestinationFinalize(output);CFRelease(output);}
                CGImageRelease(bitmap);
            }
            context=nil;
            if(gl){CGLSetCurrentContext(NULL);CGLDestroyContext(gl);}
            if(pixelFormat)CGLDestroyPixelFormat(pixelFormat);
            }
        }
    });
}
