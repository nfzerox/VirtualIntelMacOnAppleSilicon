#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <signal.h>
#include <dlfcn.h>
#include <unistd.h>

// No shader compiler is involved: clear, optional mipmap/view, GPU readback.
// A per-process timeout bounds a broken guest command channel.
int main(int argc, const char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    alarm(20);
    @autoreleasepool {
        if (argc != 2) return 64;
        NSString *mode = [NSString stringWithUTF8String:argv[1]];
        BOOL view = [mode containsString:@"view"];
        BOOL mipmaps = [mode containsString:@"mip"];
        BOOL levels = mipmaps || [mode containsString:@"levels"];
        BOOL upper = [mode containsString:@"upper"];
        BOOL array = [mode containsString:@"array"];
        BOOL private = [mode containsString:@"private"];
        NSUInteger dimension = 64;
        const char *sizeArg = getenv("PVG_TEXTURE_SIZE");
        if (sizeArg) {
            char *end = NULL;
            unsigned long parsed = strtoul(sizeArg, &end, 10);
            if (!end || *end || parsed < 64 || parsed > 2048) return 64;
            dimension = parsed;
        }
        void (*installTrace)(void) = NULL;
        const char *tracePath = getenv("PVG_RESOURCE_TRACE_DYLIB");
        if (tracePath) {
            void *trace = dlopen(tracePath, RTLD_NOW | RTLD_LOCAL);
            installTrace = trace ? dlsym(trace, "PVGInstallResourceTrace") : NULL;
            if (!installTrace) return 65;
            installTrace();
        }
        id<MTLDevice> device = MTLCopyAllDevices().firstObject;
        if (installTrace) installTrace();
        id<MTLCommandQueue> queue = [device newCommandQueue];
        if (!queue) return 1;
        MTLTextureDescriptor *desc = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
            width:dimension height:dimension mipmapped:levels];
        desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead |
                     MTLTextureUsagePixelFormatView;
        desc.storageMode = private ? MTLStorageModePrivate : MTLStorageModeShared;
        desc.allowGPUOptimizedContents = ![mode containsString:@"unoptimized"];
        if (array) {
            desc.textureType = MTLTextureType2DArray;
            desc.arrayLength = 6;
        }
        id<MTLTexture> parent = [device newTextureWithDescriptor:desc];
        printf("mode=%s device=%s parent=%p levels=%lu slices=%lu\n", argv[1],
               device.name.UTF8String, parent, (unsigned long)parent.mipmapLevelCount,
               (unsigned long)parent.arrayLength);
        if (!parent) return 2;
        id<MTLCommandBuffer> command = [queue commandBuffer];
        for (NSUInteger slice = 0; slice < desc.arrayLength; ++slice) {
            MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = parent;
            pass.colorAttachments[0].slice = slice;
            pass.colorAttachments[0].level = upper ? 1 : 0;
            pass.colorAttachments[0].loadAction = MTLLoadActionClear;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            pass.colorAttachments[0].clearColor = MTLClearColorMake(1, .5, .25, 1);
            id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
            [encoder endEncoding];
        }
        if (mipmaps) {
            id<MTLBlitCommandEncoder> encoder = [command blitCommandEncoder];
            [encoder generateMipmapsForTexture:parent];
            [encoder endEncoding];
        }
        id<MTLTexture> selected = parent;
        NSUInteger level = (mipmaps || upper) ? 1 : 0;
        NSUInteger slice = array ? 3 : 0;
        if (view) {
            selected = [parent newTextureViewWithPixelFormat:desc.pixelFormat
                textureType:MTLTextureType2D levels:NSMakeRange(level, 1)
                slices:NSMakeRange(slice, 1)];
            printf("view=%p size=%lux%lu levels=%lu type=%lu\n", selected,
                   (unsigned long)selected.width, (unsigned long)selected.height,
                   (unsigned long)selected.mipmapLevelCount,
                   (unsigned long)selected.textureType);
            if (!selected) return 3;
            level = slice = 0;
        }
        NSUInteger width = selected.width >> level;
        NSUInteger height = selected.height >> level;
        NSUInteger pitch = (width * 4 + 255) & ~255UL;
        id<MTLBuffer> output = [device newBufferWithLength:pitch * height
                                                options:MTLResourceStorageModeShared];
        memset(output.contents, 0xa5, output.length);
        id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
        [blit copyFromTexture:selected sourceSlice:slice sourceLevel:level
            sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(width, height, 1)
            toBuffer:output destinationOffset:0 destinationBytesPerRow:pitch
            destinationBytesPerImage:pitch * height];
        [blit endEncoding];
        [command commit];
        [command waitUntilCompleted];
        printf("status=%lu error=%s\n", (unsigned long)command.status,
               command.error.description.UTF8String ?: "none");
        if (command.status != MTLCommandBufferStatusCompleted) return 4;
        const unsigned char *bytes = output.contents;
        for (NSUInteger y = 0; y < height; ++y) {
            for (NSUInteger x = 0; x < width; ++x) {
                const unsigned char *p = bytes + y * pitch + x * 4;
                if (abs((int)p[0] - 64) > 1 || abs((int)p[1] - 128) > 1 ||
                    p[2] != 255 || p[3] != 255) {
                    printf("FAIL pixel=%lu,%lu BGRA=%u,%u,%u,%u\n",
                           (unsigned long)x, (unsigned long)y, p[0], p[1], p[2], p[3]);
                    return 5;
                }
            }
        }
        printf("PASS %s pixels=%lu\n", argv[1], (unsigned long)(width * height));
        return 0;
    }
}
