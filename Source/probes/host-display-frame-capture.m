// One-shot diagnostic of donor display input/output, not a production hook.
// Copies after donor encoding on the same command buffer, preserving its waits.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <IOSurface/IOSurface.h>
#import <objc/runtime.h>
static IMP original;
static unsigned armed = 1;
void PVGArmDisplayCapture(void) { __atomic_store_n(&armed, 1, __ATOMIC_RELAXED); }
static void dump_surface(IOSurfaceRef surface, NSString *name, BOOL bgra) {
    BOOL planar = IOSurfaceGetPlaneCount(surface) != 0;
    size_t w = planar ? IOSurfaceGetWidthOfPlane(surface, 0) : IOSurfaceGetWidth(surface);
    size_t h = planar ? IOSurfaceGetHeightOfPlane(surface, 0) : IOSurfaceGetHeight(surface);
    size_t row = planar ? IOSurfaceGetBytesPerRowOfPlane(surface, 0) : IOSurfaceGetBytesPerRow(surface);
    size_t bpe = planar ? IOSurfaceGetBytesPerElementOfPlane(surface, 0) : IOSurfaceGetBytesPerElement(surface);
    NSLog(@"PVG CPU surface %@ planes=%zu w=%zu h=%zu row=%zu bpe=%zu", name,
          IOSurfaceGetPlaneCount(surface), w, h, row, bpe);
    if (bpe != 4 || row < w * 4) return;
    const uint8_t *bytes = planar ? IOSurfaceGetBaseAddressOfPlane(surface, 0) : IOSurfaceGetBaseAddress(surface);
    if (!bytes) return;
    FILE *f = fopen([[NSString stringWithFormat:@"/tmp/pvg-display-%@.ppm", name]
                    fileSystemRepresentation], "wb");
    if (!f) return;
    fprintf(f, "P6\n%zu %zu\n255\n", w, h);
    for (size_t y=0; y<h; y++) for (size_t x=0; x<w; x++) {
        const uint8_t *p = bytes+y*row+x*4;
        uint8_t rgb[3] = {p[bgra ? 2 : 0], p[1], p[bgra ? 0 : 2]};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}
static void capture(id<MTLTexture> texture, id<MTLCommandBuffer> command,
                    NSString *name) {
    NSLog(@"PVG display %@ texture=%@ parent=%@ buffer=%@ offset=%lu row=%lu iosurface=%p",
          name, texture, texture.parentTexture, texture.buffer,
          texture.bufferOffset, texture.bufferBytesPerRow, texture.iosurface);
    if (!texture || texture.textureType != MTLTextureType2D ||
        (texture.pixelFormat != MTLPixelFormatBGRA8Unorm &&
         texture.pixelFormat != MTLPixelFormatBGRA8Unorm_sRGB &&
         texture.pixelFormat != MTLPixelFormatRGBA8Unorm)) {
        NSLog(@"PVG capture unsupported %@ %@", name, texture);
        return;
    }
    NSUInteger w = texture.width, h = texture.height, row = (w * 4 + 255) & ~255UL;
    BOOL bgra = texture.pixelFormat != MTLPixelFormatRGBA8Unorm;
    id<MTLBuffer> buffer = [texture.device newBufferWithLength:row * h
                                                    options:MTLResourceStorageModeShared];
    if (!buffer) return;
    id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
    [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0
        sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(w,h,1)
        toBuffer:buffer destinationOffset:0 destinationBytesPerRow:row
        destinationBytesPerImage:row*h];
    [blit endEncoding];
    [command addCompletedHandler:^(id<MTLCommandBuffer> completed) {
        if (completed.status != MTLCommandBufferStatusCompleted) return;
        NSString *path = [@"/tmp/pvg-display-" stringByAppendingFormat:@"%@.ppm", name];
        FILE *f = fopen(path.fileSystemRepresentation, "wb");
        if (!f) return;
        fprintf(f, "P6\n%lu %lu\n255\n", w, h);
        const uint8_t *bytes = buffer.contents;
        for (NSUInteger y=0; y<h; y++) for (NSUInteger x=0; x<w; x++) {
            const uint8_t *p = bytes+y*row+x*4;
            uint8_t rgb[3] = {p[bgra ? 2 : 0], p[1], p[bgra ? 0 : 2]};
            fwrite(rgb, 1, 3, f);
        }
        fclose(f);
        if ([name isEqualToString:@"input"] && texture.iosurface) {
            IOSurfaceRef surface = texture.iosurface;
            // Unsynchronized bytes are diagnostic only, not a coherent snapshot.
            dump_surface(surface, @"cpu-unlocked", bgra);
            IOReturn result = IOSurfaceLock(surface, kIOSurfaceLockReadOnly, NULL);
            if (result == kIOReturnSuccess) {
                dump_surface(surface, @"cpu-locked", bgra);
                IOSurfaceUnlock(surface, kIOSurfaceLockReadOnly, NULL);
            }
            NSLog(@"PVG surface read lock result=0x%x", result);
        }
        NSLog(@"PVG captured %@", path);
    }];
    [buffer release];
}
static void render(id self, SEL cmd, id input, id output, id command, MTLViewport viewport) {
    ((void (*)(id, SEL, id, id, id, MTLViewport))original)
        (self, cmd, input, output, command, viewport);
    if (__atomic_exchange_n(&armed, 0, __ATOMIC_RELAXED)) {
        capture(input, command, @"input");
        capture(output, command, @"output");
    }
}
__attribute__((constructor)) static void install(void) {
    Class cls = NSClassFromString(@"_PGDisplay");
    SEL sel = sel_registerName("encodeRenderFrame:toDisplay:withCmdBuf:viewport:");
    Method method = class_getInstanceMethod(cls, sel);
    if (!method) abort();
    original = method_getImplementation(method);
    class_replaceMethod(cls, sel, (IMP)render, method_getTypeEncoding(method));
}
