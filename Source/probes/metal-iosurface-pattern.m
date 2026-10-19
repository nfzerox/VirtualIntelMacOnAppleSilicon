// Bounded, explicit IOSurface import diagnostic. No production hooks.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <IOSurface/IOSurface.h>
#import <CoreImage/CoreImage.h>
#include <unistd.h>
#include <dlfcn.h>
#import <objc/runtime.h>
#include <execinfo.h>
static void pixel(unsigned x,unsigned y,unsigned char *p) {
    p[0]=(x*17+y*3)&255;p[1]=(x*5+y*23)&255;p[2]=(x^(y*11))&255;p[3]=255;
}
static IMP nativeSurfaceInit,nativeViewInit,nativeBufferInit,nativeTiledInit;
static id traceBufferInit(id self,SEL sel,id buffer,MTLTextureDescriptor *d,NSUInteger offset,NSUInteger row) {
    IMP native=sel==sel_registerName("initTiledWithBuffer:descriptor:offset:bytesPerRow:")?nativeTiledInit:nativeBufferInit;
    id result=((id(*)(id,SEL,id,id,NSUInteger,NSUInteger))native)(self,sel,buffer,d,offset,row);
    fprintf(stderr,"%s texture=%p %lux%lu format=%lu storage=%lu usage=%lu offset=%lu row=%lu buffer=%s\n",
        sel_getName(sel),(__bridge void *)result,d.width,d.height,d.pixelFormat,d.storageMode,d.usage,offset,row,
        [[buffer description] UTF8String]);
    return result;
}
static id traceSurfaceInit(id self,SEL sel,id device,MTLTextureDescriptor *d,IOSurfaceRef surface,NSUInteger plane,NSUInteger field) {
    id result=((id(*)(id,SEL,id,id,IOSurfaceRef,NSUInteger,NSUInteger))nativeSurfaceInit)(self,sel,device,d,surface,plane,field);
    fprintf(stderr,"Surface texture %p %lux%lu format=%lu storage=%lu usage=%lu surface=%u %zux%zu row=%zu plane=%lu field=%lu\n",
        (__bridge void *)result,d.width,d.height,d.pixelFormat,d.storageMode,d.usage,IOSurfaceGetID(surface),
        IOSurfaceGetWidth(surface),IOSurfaceGetHeight(surface),IOSurfaceGetBytesPerRow(surface),plane,field);
    if(getenv("PVG_PATTERN_SURFACE_STACK")) {
        fprintf(stderr,"Surface properties %s\n",[CFBridgingRelease(IOSurfaceCopyAllValues(surface)) description].UTF8String);
        void *frames[18];int n=backtrace(frames,18);backtrace_symbols_fd(frames,n,STDERR_FILENO);
    }
    return result;
}
static id traceViewInit(id self,SEL sel,id parent,NSUInteger format,NSUInteger type,NSRange levels,NSRange slices,MTLTextureSwizzleChannels swizzle) {
    id result=((id(*)(id,SEL,id,NSUInteger,NSUInteger,NSRange,NSRange,MTLTextureSwizzleChannels))nativeViewInit)
        (self,sel,parent,format,type,levels,slices,swizzle);
    fprintf(stderr,"Texture view %p parent=%p format=%lu levels=%lu,%lu slices=%lu,%lu\n",
        (__bridge void *)result,(__bridge void *)parent,format,levels.location,levels.length,slices.location,slices.length);
    return result;
}
int main(int argc,char **argv) {
    alarm(20);
    @autoreleasepool {
        if(getenv("PVG_PATTERN_LOAD_GL_HELPER")) {
            void *helper=dlopen("/Library/GPUBundles/AppleMetalOpenGLRenderer.bundle/Contents/MacOS/PVGOpenGLMetal.dylib",RTLD_NOW);
            if(!helper){fprintf(stderr,"helper: %s\n",dlerror());return 3;}
        }
        if(argc!=4)return 64;
        unsigned w=atoi(argv[1]),h=atoi(argv[2]);
        if(!w||!h||w>8192||h>8192)return 64;
        BOOL coreImage=!strcmp(argv[3],"ci");
        BOOL managed=!strcmp(argv[3],"managed");
        if(!coreImage&&!managed&&strcmp(argv[3],"shared"))return 64;
        size_t row=(w*4+255)&~255UL;
        if(getenv("PVG_SOURCE_ROW_BYTES"))row=strtoul(getenv("PVG_SOURCE_ROW_BYTES"),NULL,10);
        if(row<w*4||row>w*4+4096)return 64;
        IOSurfaceRef surface=IOSurfaceCreate((CFDictionaryRef)@{
            (id)kIOSurfaceWidth:@(w),(id)kIOSurfaceHeight:@(h),(id)kIOSurfaceBytesPerElement:@4,
            (id)kIOSurfaceBytesPerRow:@(row),(id)kIOSurfacePixelFormat:@(0x52474241),
            @"IOSurfaceCacheMode":@0,@"IOSurfacePixelSizeCastingAllowed":@YES});
        if(!surface||IOSurfaceLock(surface,0,NULL))return 1;
        unsigned char *bytes=IOSurfaceGetBaseAddress(surface);
        for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)pixel(x,y,bytes+y*row+x*4);
        IOSurfaceUnlock(surface,0,NULL);
        id<MTLDevice> device=MTLCreateSystemDefaultDevice();
        if(!coreImage&&!managed&&![device supportsFamily:MTLGPUFamilyApple1]) {
            fprintf(stderr,"Shared textures are unsupported on non-Apple GPUs; not submitting.\n");return 64;
        }
        if(getenv("PVG_PATTERN_TRACE_TEXTURES")) {
            Class cls=NSClassFromString(@"AppleParavirtTexture");
            SEL init=sel_registerName("initWithDevice:descriptor:iosurface:plane:field:");
            Method m=class_getInstanceMethod(cls,init);nativeSurfaceInit=method_getImplementation(m);
            class_replaceMethod(cls,init,(IMP)traceSurfaceInit,method_getTypeEncoding(m));
            init=sel_registerName("initWithTexture:pixelFormat:textureType:levels:slices:swizzle:");
            m=class_getInstanceMethod(cls,init);nativeViewInit=method_getImplementation(m);
            class_replaceMethod(cls,init,(IMP)traceViewInit,method_getTypeEncoding(m));
            init=sel_registerName("initWithBuffer:descriptor:offset:bytesPerRow:");
            m=class_getInstanceMethod(cls,init);nativeBufferInit=method_getImplementation(m);
            class_replaceMethod(cls,init,(IMP)traceBufferInit,method_getTypeEncoding(m));
            init=sel_registerName("initTiledWithBuffer:descriptor:offset:bytesPerRow:");
            m=class_getInstanceMethod(cls,init);nativeTiledInit=method_getImplementation(m);
            class_replaceMethod(cls,init,(IMP)traceBufferInit,method_getTypeEncoding(m));
        }
        if(getenv("PVG_PATTERN_METHODS")) {
            for(NSString *name in @[@"AppleParavirtDevice",@"AppleParavirtTexture",@"MTLIOAccelTexture",@"_MTLTexture"]){
                Class cls=NSClassFromString(name);unsigned count=0;Method *methods=class_copyMethodList(cls,&count);
                for(unsigned i=0;i<count;i++) {
                    const char *selector=sel_getName(method_getName(methods[i]));
                    if(getenv("PVG_PATTERN_COMPILER_METHODS")&&!strstr(selector,"ompil")&&!strstr(selector,"lugin"))continue;
                    fprintf(stderr,"%s %s %s\n",name.UTF8String,selector,method_getTypeEncoding(methods[i]));
                }
                free(methods);
            }
            return 0;
        }
#if PVG_PATTERN_TRACE
        extern void PVGInstallCommandTrace(void);
        PVGInstallCommandTrace();
        fprintf(stderr,"Command trace: /tmp/pvg-command-%d\n",getpid());
#endif
        unsigned ow=w,oh=h;
        if(getenv("PVG_OUTPUT_SIZE"))ow=oh=atoi(getenv("PVG_OUTPUT_SIZE"));
        if(!ow||!oh||ow>w||oh>h)return 64;
        size_t outputRow=(ow*4+255)&~255UL;
        size_t length=outputRow*oh;unsigned char *output=calloc(1,length);
        NSString *error=@"none";NSUInteger status=0;
        if(coreImage) {
            CIContext *context=[CIContext contextWithMTLDevice:device];
            CGColorSpaceRef color=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
            CIImage *image=[CIImage imageWithIOSurface:surface options:@{kCIImageColorSpace:(__bridge id)color}];
            [context render:image toBitmap:output rowBytes:outputRow bounds:CGRectMake(0,0,ow,oh)
                format:kCIFormatRGBA8 colorSpace:color];
            CGColorSpaceRelease(color);
        } else {
            MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                width:w height:h mipmapped:NO];
            d.storageMode=managed?MTLStorageModeManaged:MTLStorageModeShared;d.usage=MTLTextureUsageShaderRead;
            id<MTLTexture> texture=[device newTextureWithDescriptor:d iosurface:surface plane:0];
            if(!texture)return 2;
            id<MTLBuffer> buffer=[device newBufferWithLength:length options:MTLResourceStorageModeShared];
            id<MTLCommandBuffer> command=[[device newCommandQueue] commandBuffer];
            id<MTLBlitCommandEncoder> encoder=[command blitCommandEncoder];
            [encoder copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                sourceSize:MTLSizeMake(ow,oh,1) toBuffer:buffer destinationOffset:0
                destinationBytesPerRow:outputRow destinationBytesPerImage:length];
            [encoder endEncoding];[command commit];[command waitUntilCompleted];
            status=command.status;error=command.error.description?:@"none";
            memcpy(output,buffer.contents,length);
        }
        uint64_t bad=0,sum=0,maxDifference=0;
        for(unsigned y=0;y<oh;y++)for(unsigned x=0;x<ow;x++) {
            // CI's crop origin is bottom-left; the returned bitmap rows keep
            // the IOSurface's top-down order within that selected region.
            unsigned char expected[4];pixel(x,y+(coreImage?h-oh:0),expected);
            BOOL mismatch=NO;
            for(unsigned c=0;c<4;c++) {
                unsigned value=output[y*outputRow+x*4+c],delta=abs((int)value-expected[c]);
                sum+=value;if(delta>maxDifference)maxDifference=delta;
                // CI color round trips may differ by one quantization step.
                if(delta>(coreImage?1:0))mismatch=YES;
            }
            if(mismatch)bad++;
        }
        NSDictionary *result=@{@"Width":@(w),@"Height":@(h),@"Mode":@(argv[3]),
            @"RowBytes":@(row),@"OutputWidth":@(ow),@"OutputHeight":@(oh),
            @"Device":device.name?:@"nil",@"UnifiedMemory":@(device.hasUnifiedMemory),
            @"BadPixels":@(bad),@"MaximumDifference":@(maxDifference),@"Sum":@(sum),
            @"CommandStatus":@(status),@"Error":error};
        puts([[NSString alloc] initWithData:[NSJSONSerialization dataWithJSONObject:result options:0 error:NULL]
            encoding:NSUTF8StringEncoding].UTF8String);
        free(output);CFRelease(surface);
        return bad||(!coreImage&&status!=MTLCommandBufferStatusCompleted);
    }
}
