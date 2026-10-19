// Explicit debugger diagnostic, never injected or installed in production.
#import <AppKit/AppKit.h>
#import <CoreImage/CoreImage.h>
#import <Metal/Metal.h>
#import <objc/runtime.h>
static IMP originalSurfaceTexture;
static _Thread_local __unsafe_unretained NSMutableArray *surfaceTextureTrace;
static id traceSurfaceTexture(id self,SEL selector,MTLTextureDescriptor *descriptor,IOSurfaceRef surface,NSUInteger plane) {
    id result=((id(*)(id,SEL,id,IOSurfaceRef,NSUInteger))originalSurfaceTexture)(self,selector,descriptor,surface,plane);
    if(surfaceTextureTrace.count<64&&surfaceTextureTrace)
        [surfaceTextureTrace addObject:@{@"Descriptor":descriptor.description?:@"nil",
            @"Texture":[result description]?:@"nil",@"Surface":@(IOSurfaceGetID(surface))}];
    return result;
}
id VIMPreviewTraceSurfaceImport(IOSurfaceRef surface) {
    id<MTLDevice> device=MTLCreateSystemDefaultDevice();
    Method method=class_getInstanceMethod([device class],@selector(newTextureWithDescriptor:iosurface:plane:));
    if(!originalSurfaceTexture) {
        originalSurfaceTexture=method_getImplementation(method);
        class_replaceMethod([device class],@selector(newTextureWithDescriptor:iosurface:plane:),
            (IMP)traceSurfaceTexture,method_getTypeEncoding(method));
    }
    CFRetain(surface);
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0), ^{
        @autoreleasepool {
            NSMutableArray *trace=[NSMutableArray array];
            surfaceTextureTrace=trace;
            CIContext *context=[CIContext contextWithMTLDevice:device];
            CGColorSpaceRef color=CGColorSpaceCreateDeviceRGB();
            unsigned char output[128*128*4]={0};
            [context render:[CIImage imageWithIOSurface:surface] toBitmap:output rowBytes:512
                bounds:CGRectMake(0,0,128,128) format:kCIFormatRGBA8 colorSpace:color];
            CGColorSpaceRelease(color);CFRelease(surface);
            uint64_t sum=0;for(unsigned i=0;i<sizeof(output);i++)sum+=output[i];
            NSDictionary *result=@{@"Textures":surfaceTextureTrace,@"Sum":@(sum)};
            [[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:NULL]
                writeToFile:[NSHomeDirectory() stringByAppendingPathComponent:@"tmp/vim-preview-import-trace.json"] atomically:YES];
            surfaceTextureTrace=nil;
        }
    });
    return @"Scheduled traced Core Image import; detach";
}
id VIMPreviewCompareSurfaceAliases(IOSurfaceRef surface) {
    CFRetain(surface);
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0), ^{
        @autoreleasepool {
            id<MTLDevice> device=MTLCreateSystemDefaultDevice();
            size_t w=IOSurfaceGetWidth(surface),h=IOSurfaceGetHeight(surface),row=IOSurfaceGetBytesPerRow(surface);
            IOSurfaceRef clone=IOSurfaceCreate((CFDictionaryRef)@{(id)kIOSurfaceWidth:@(w),
                (id)kIOSurfaceHeight:@(h),(id)kIOSurfaceBytesPerElement:@4,
                (id)kIOSurfaceBytesPerRow:@(row),(id)kIOSurfacePixelFormat:@(IOSurfaceGetPixelFormat(surface))});
            IOSurfaceLock(surface,kIOSurfaceLockReadOnly,NULL);IOSurfaceLock(clone,0,NULL);
            memcpy(IOSurfaceGetBaseAddress(clone),IOSurfaceGetBaseAddress(surface),row*h);
            IOSurfaceUnlock(clone,0,NULL);IOSurfaceUnlock(surface,kIOSurfaceLockReadOnly,NULL);
            MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                width:w height:h mipmapped:NO];d.storageMode=MTLStorageModeManaged;d.usage=MTLTextureUsageShaderRead;
            id<MTLTexture> texture=[device newTextureWithDescriptor:d iosurface:surface plane:0];
            CGColorSpaceRef srgb=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
            NSArray *images=@[[CIImage imageWithIOSurface:surface],[CIImage imageWithIOSurface:clone],
                [CIImage imageWithMTLTexture:texture options:nil],
                [CIImage imageWithIOSurface:surface options:@{kCIImageColorSpace:NSNull.null}],
                [CIImage imageWithIOSurface:surface options:@{kCIImageColorSpace:(__bridge id)srgb}],
                [CIImage imageWithMTLTexture:texture options:@{kCIImageColorSpace:(__bridge id)srgb}]];
            CGColorSpaceRelease(srgb);
            CIContext *context=[CIContext contextWithMTLDevice:device];
            CGColorSpaceRef color=CGColorSpaceCreateDeviceRGB();
            NSMutableArray *results=[NSMutableArray array];
            for(unsigned mode=0;mode<images.count;mode++) {
                unsigned char output[128*128*4]={0};
                [context render:images[mode] toBitmap:output rowBytes:512
                    bounds:CGRectMake(0,0,128,128) format:kCIFormatRGBA8 colorSpace:color];
                uint64_t rgb=0,alpha=0;
                for(unsigned i=0;i<128*128;i++){rgb+=output[i*4]+output[i*4+1]+output[i*4+2];alpha+=output[i*4+3];}
                [results addObject:@{@"Mode":@(mode),@"RGB":@(rgb),@"Alpha":@(alpha)}];
            }
            CGColorSpaceRelease(color);CFRelease(clone);CFRelease(surface);
            [[NSJSONSerialization dataWithJSONObject:results options:NSJSONWritingPrettyPrinted error:NULL]
                writeToFile:[NSHomeDirectory() stringByAppendingPathComponent:@"tmp/vim-preview-alias-check.json"] atomically:YES];
        }
    });
    return @"Scheduled original / copied IOSurface / explicit managed texture comparison; detach";
}
static id ImageContentView(void) {
    NSMutableArray *pending=[NSApplication.sharedApplication.windows mutableCopy];
    Class imageClass=NSClassFromString(@"IKImageContentView");
    for(NSUInteger visited=0;pending.count&&visited<4096;visited++) {
        id view=pending.lastObject;[pending removeLastObject];
        if(imageClass&&[view isKindOfClass:imageClass]) {
            return view;
        }
        if([view isKindOfClass:NSWindow.class]) {
            if([view contentView])[pending addObject:[view contentView]];
        } else if([view isKindOfClass:NSView.class]) [pending addObjectsFromArray:[view subviews]];
    }
    return nil;
}
id VIMPreviewTileContext(void) {
    id view=ImageContentView();
    if(!view)return @{@"Error":@"No image content view in Preview's windows"};
    id info=[view valueForKey:@"_renderInfo"];
    id<MTLDevice> device=MTLCreateSystemDefaultDevice();
    return @{@"Device":device.description?:@"nil",@"UnifiedMemory":@(device.hasUnifiedMemory),
        @"View":[view description],
        @"SoftwareRenderer":[info valueForKey:@"_usingSoftwareRenderer"]?:NSNull.null,
        @"Context":[[info valueForKey:@"_ciContext"] description]?:@"nil",
        @"DrawingImage":[[view valueForKey:@"_drawingCIImage"] debugDescription]?:@"nil"};
}
id VIMPreviewSurfaceInfo(IOSurfaceRef surface) {
    if(!surface)return @"No surface";
    return @{@"Width":@(IOSurfaceGetWidth(surface)),@"Height":@(IOSurfaceGetHeight(surface)),
        @"RowBytes":@(IOSurfaceGetBytesPerRow(surface)),@"Allocation":@(IOSurfaceGetAllocSize(surface)),
        @"ElementBytes":@(IOSurfaceGetBytesPerElement(surface)),@"Format":@(IOSurfaceGetPixelFormat(surface)),
        @"Planes":@(IOSurfaceGetPlaneCount(surface)),@"Seed":@(IOSurfaceGetSeed(surface)),
        @"Properties":CFBridgingRelease(IOSurfaceCopyAllValues(surface))?:@{}};
}
id VIMPreviewCheckTileImages(void) {
    id view=ImageContentView();
    if(!view)return @"No image view";
    CIContext *actual=[[view valueForKey:@"_renderInfo"] valueForKey:@"_ciContext"];
    NSArray *images=@[[view valueForKey:@"_ciImage"],[view valueForKey:@"_drawingCIImage"]];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0), ^{
        @autoreleasepool {
            NSMutableArray *results=[NSMutableArray array];
            NSArray *contexts=@[actual,[CIContext contextWithMTLDevice:MTLCreateSystemDefaultDevice()],
                [CIContext contextWithOptions:@{kCIContextUseSoftwareRenderer:@YES}]];
            CGColorSpaceRef color=CGColorSpaceCreateDeviceRGB();
            for(NSUInteger imageIndex=0;imageIndex<images.count;imageIndex++) {
                CIImage *image=images[imageIndex];
                for(NSUInteger mode=0;mode<contexts.count;mode++) {
                    unsigned char pixels[128*128*4]={0};
                    [contexts[mode] render:image toBitmap:pixels rowBytes:128*4
                        bounds:CGRectMake(0,0,128,128) format:kCIFormatRGBA8 colorSpace:color];
                    uint64_t rgb=0,alpha=0;
                    for(unsigned i=0;i<128*128;i++) {
                        rgb+=pixels[i*4]+pixels[i*4+1]+pixels[i*4+2];alpha+=pixels[i*4+3];
                    }
                    [results addObject:@{@"Image":@(imageIndex),@"Context":@(mode),@"RGB":@(rgb),@"Alpha":@(alpha)}];
                }
            }
            CGColorSpaceRelease(color);
            [[NSJSONSerialization dataWithJSONObject:results options:NSJSONWritingPrettyPrinted error:NULL]
                writeToFile:[NSHomeDirectory() stringByAppendingPathComponent:@"tmp/vim-preview-image-check.json"] atomically:YES];
        }
    });
    return @"Scheduled; detach before reading the result in Preview's container tmp directory";
}
id VIMPreviewCheckRawSurface(IOSurfaceRef surface) {
    if(!surface)return @"No surface";
    CIImage *image=[CIImage imageWithIOSurface:surface];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0), ^{
        @autoreleasepool {
            NSMutableArray *results=[NSMutableArray array];
            NSArray *contexts=@[[CIContext contextWithMTLDevice:MTLCreateSystemDefaultDevice()],
                [CIContext contextWithOptions:@{kCIContextUseSoftwareRenderer:@YES}]];
            CGColorSpaceRef color=CGColorSpaceCreateDeviceRGB();
            for(NSUInteger mode=0;mode<contexts.count;mode++) {
                unsigned char pixels[128*128*4]={0};
                [contexts[mode] render:image toBitmap:pixels rowBytes:128*4
                    bounds:CGRectMake(0,0,128,128) format:kCIFormatRGBA8 colorSpace:color];
                uint64_t rgb=0,alpha=0;
                for(unsigned i=0;i<128*128;i++) {
                    rgb+=pixels[i*4]+pixels[i*4+1]+pixels[i*4+2];alpha+=pixels[i*4+3];
                }
                [results addObject:@{@"Context":@(mode),@"RGB":@(rgb),@"Alpha":@(alpha)}];
            }
            CGColorSpaceRelease(color);
            [[NSJSONSerialization dataWithJSONObject:results options:NSJSONWritingPrettyPrinted error:NULL]
                writeToFile:[NSHomeDirectory() stringByAppendingPathComponent:@"tmp/vim-preview-surface-check.json"] atomically:YES];
        }
    });
    return @"Scheduled raw surface comparison; detach";
}
id VIMPreviewBlitSurface(IOSurfaceRef surface) {
    if(!surface)return @"No surface";
    CFRetain(surface);
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0), ^{
        @autoreleasepool {
            id<MTLDevice> device=MTLCreateSystemDefaultDevice();
            id<MTLCommandQueue> queue=[device newCommandQueue];
            NSMutableArray *results=[NSMutableArray array];
            // Shared textures are invalid on Intel GPUs; do not fault the
            // live application by treating that unsupported mode as an A/B.
            for(NSUInteger storage=MTLStorageModeManaged;storage<=MTLStorageModeManaged;storage++) {
                MTLTextureDescriptor *desc=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                    width:IOSurfaceGetWidth(surface) height:IOSurfaceGetHeight(surface) mipmapped:NO];
                desc.storageMode=storage;desc.usage=MTLTextureUsageShaderRead;
                id<MTLTexture> texture=[device newTextureWithDescriptor:desc iosurface:surface plane:0];
                id<MTLBuffer> output=[device newBufferWithLength:128*128*4 options:MTLResourceStorageModeShared];
                id<MTLCommandBuffer> command=[queue commandBuffer];
                id<MTLBlitCommandEncoder> encoder=[command blitCommandEncoder];
                if(texture)[encoder copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                    sourceSize:MTLSizeMake(128,128,1) toBuffer:output destinationOffset:0
                    destinationBytesPerRow:128*4 destinationBytesPerImage:128*128*4];
                [encoder endEncoding];[command commit];[command waitUntilCompleted];
                uint64_t rgb=0,alpha=0;const unsigned char *pixels=output.contents;
                for(unsigned i=0;i<128*128;i++) {
                    rgb+=pixels[i*4]+pixels[i*4+1]+pixels[i*4+2];alpha+=pixels[i*4+3];
                }
                [results addObject:@{@"Storage":@(storage),@"Texture":texture.description?:@"nil",
                    @"RGB":@(rgb),@"Alpha":@(alpha),@"Status":@(command.status),@"Error":command.error.description?:@"none"}];
            }
            CFRelease(surface);
            [[NSJSONSerialization dataWithJSONObject:results options:NSJSONWritingPrettyPrinted error:NULL]
                writeToFile:[NSHomeDirectory() stringByAppendingPathComponent:@"tmp/vim-preview-blit-check.json"] atomically:YES];
        }
    });
    return @"Scheduled direct surface blit; detach";
}
