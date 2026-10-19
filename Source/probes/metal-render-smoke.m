#import <Foundation/Foundation.h>
#import <IOSurface/IOSurface.h>
#import <Metal/Metal.h>

#include <dlfcn.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <stdio.h>
#include <unistd.h>

static void inspectFunctionData(id<MTLFunction> function) {
    SEL functionDataSelector = sel_registerName("functionData");
    SEL libraryDataSelector = sel_registerName("libraryData");
    void *(*sendPointer)(id, SEL) = (void *(*)(id, SEL))objc_msgSend;
    if (![function respondsToSelector:functionDataSelector] ||
        ![function respondsToSelector:libraryDataSelector]) {
        fprintf(stderr, "%s has no private data accessors\n",
                function.name.UTF8String);
        return;
    }

    const uint8_t *functionData = sendPointer(function, functionDataSelector);
    const uint8_t *libraryData = sendPointer(function, libraryDataSelector);
    if (!functionData || !libraryData) {
        fprintf(stderr, "%s functionData=%p libraryData=%p\n",
                function.name.UTF8String, functionData, libraryData);
        return;
    }

    uint64_t bitcodeOffset = 0;
    uint64_t bitcodeFileSize = 0;
    uint16_t airMajor = 0;
    uint16_t airMinor = 0;
    memcpy(&bitcodeOffset, functionData + 0x00, sizeof(bitcodeOffset));
    memcpy(&bitcodeFileSize, functionData + 0x08, sizeof(bitcodeFileSize));
    memcpy(&airMajor, functionData + 0x28, sizeof(airMajor));
    memcpy(&airMinor, functionData + 0x2a, sizeof(airMinor));

    uint32_t libraryMagic = 0;
    memcpy(&libraryMagic, libraryData, sizeof(libraryMagic));
    fprintf(stderr,
            "%s class=%s functionData=%p libraryData=%p bitcodeOffset=0x%llx "
            "bitcodeFileSize=0x%llx AIR=%u.%u libraryMagic=0x%08x\n",
            function.name.UTF8String, object_getClassName(function),
            functionData, libraryData, bitcodeOffset, bitcodeFileSize,
            airMajor, airMinor, libraryMagic);
    if (libraryMagic == UINT32_C(0x424c544d))
        return;

    const uint8_t *mappedBytes = NULL;
    size_t mappedLength = 0;
    memcpy(&mappedBytes, libraryData + 0x48, sizeof(mappedBytes));
    memcpy(&mappedLength, libraryData + 0x50, sizeof(mappedLength));
    fprintf(stderr, "  mappedBytes=%p mappedLength=0x%zx offset bytes:",
            mappedBytes, mappedLength);
    if (mappedBytes && bitcodeOffset < mappedLength) {
        size_t available = mappedLength - (size_t)bitcodeOffset;
        size_t count = available < 32 ? available : 32;
        for (size_t index = 0; index < count; ++index)
            fprintf(stderr, " %02x", mappedBytes[bitcodeOffset + index]);
    }
    fputc('\n', stderr);
}

int main(void) {
    @autoreleasepool {
        // Install the superclass resource hook before Metal creates the device.
        // AppleParavirtTexture can otherwise cache the inherited initializer's
        // IMP during device setup and evade a later diagnostic swizzle.
        const char *resourceTraceDylib = getenv("PVG_RESOURCE_TRACE_DYLIB");
        void (*installResourceTrace)(void) = NULL;
        if (resourceTraceDylib && *resourceTraceDylib) {
            void *traceImage = dlopen(resourceTraceDylib,
                                      RTLD_NOW | RTLD_LOCAL);
            installResourceTrace = traceImage
                ? dlsym(traceImage, "PVGInstallResourceTrace") : NULL;
            if (!installResourceTrace) {
                fprintf(stderr, "resource trace load failed: %s\n", dlerror());
                return 1;
            }
            installResourceTrace();
        }

        id<MTLDevice> device = MTLCopyAllDevices().firstObject;
        if (!device) {
            fprintf(stderr, "no Metal device\n");
            return 1;
        }
        fprintf(stderr, "device class=%s\n", object_getClassName(device));

        // On native Big Sur the MTLIOAccelResource class is instantiated by
        // device setup, after the pre-device hook attempt above. Repeating the
        // idempotent installer here captures the first IOSurface-backed
        // texture while preserving the earlier Catalina/PVG hook ordering.
        if (installResourceTrace)
            installResourceTrace();

        if (getenv("PVG_PRELOAD_SERIALIZER")) {
            void *serializer = dlopen(
                "/System/Library/PrivateFrameworks/MetalSerializer.framework/"
                "Versions/A/MetalSerializer", RTLD_NOW | RTLD_LOCAL);
            if (!serializer) {
                fprintf(stderr, "serializer preload failed: %s\n", dlerror());
                return 1;
            }
            const char *(*buildIdentifier)(void) =
                dlsym(serializer, "PVGMetalCompatBuildIdentifier");
            void (*installPipelineHooks)(void) =
                dlsym(serializer, "PVGInstallMetalCompatPipelineHooks");
            fprintf(stderr, "serializer shim=%s installer=%s\n",
                    buildIdentifier ? buildIdentifier() : "missing",
                    installPipelineHooks ? "present" : "missing");
            if (getenv("PVG_FORCE_PIPELINE_HOOK") && installPipelineHooks)
                installPipelineHooks();
        }

        const char *traceDylib = getenv("PVG_COMMAND_TRACE_DYLIB");
        if (traceDylib && *traceDylib) {
            void *traceImage = dlopen(traceDylib, RTLD_NOW | RTLD_LOCAL);
            void (*installTrace)(void) = traceImage
                ? dlsym(traceImage, "PVGInstallCommandTrace") : NULL;
            if (!installTrace) {
                fprintf(stderr, "command trace load failed: %s\n", dlerror());
                return 1;
            }
            installTrace();
        }

        bool useCompositor = getenv("PVG_RENDER_COMPOSITOR") != NULL;
        bool useWindowServerCopy =
            getenv("PVG_RENDER_WINDOWSERVER_COPY") != NULL;
        bool useCompositorVertexBuffer =
            getenv("PVG_RENDER_COMPOSITOR_NO_VERTEX_BUFFER") == NULL;
        const char *sourceCountValue =
            getenv("PVG_RENDER_COMPOSITOR_TEXTURE_COUNT");
        NSUInteger compositorTextureCount = sourceCountValue
            ? (NSUInteger)strtoull(sourceCountValue, NULL, 0) : 3;
        if (compositorTextureCount < 1 || compositorTextureCount > 3) {
            fprintf(stderr, "compositor texture count must be 1..3\n");
            return 1;
        }
        NSString *source =
            @"#include <metal_stdlib>\n"
             "using namespace metal;\n"
             "struct PVGVarying { float4 position [[position]]; float2 uv; };\n"
             "vertex float4 pvgVertex(uint vertexID [[vertex_id]]) {\n"
             "  const float2 p[3] = {float2(-1.0, -1.0),\n"
             "                       float2( 3.0, -1.0),\n"
             "                       float2(-1.0,  3.0)};\n"
             "  return float4(p[vertexID], 0.0, 1.0);\n"
             "}\n"
             "fragment float4 pvgFragment() {\n"
             "  return float4(0.25, 0.5, 0.75, 1.0);\n"
             "}\n"
             "vertex PVGVarying pvgCompositorVertex(\n"
             "    const device float4 *vertices [[buffer(0)]],\n"
             "    uint vertexID [[vertex_id]]) {\n"
             "  PVGVarying out;\n"
             "  out.position = float4(vertices[vertexID].xy, 0.0, 1.0);\n"
             "  out.uv = vertices[vertexID].zw;\n"
             "  return out;\n"
             "}\n"
             "vertex PVGVarying pvgSampleVertex(uint vertexID [[vertex_id]]) {\n"
             "  const float4 vertices[3] = {\n"
             "    float4(-1.0, -1.0, 0.0, 1.0),\n"
             "    float4( 3.0, -1.0, 2.0, 1.0),\n"
             "    float4(-1.0,  3.0, 0.0, -1.0)};\n"
             "  PVGVarying out;\n"
             "  out.position = float4(vertices[vertexID].xy, 0.0, 1.0);\n"
             "  out.uv = vertices[vertexID].zw;\n"
             "  return out;\n"
             "}\n"
             "fragment float4 pvgCompositorFragment(\n"
             "    PVGVarying in [[stage_in]],\n"
             "    texture2d<float> source [[texture(3)]],\n"
             "    sampler sourceSampler [[sampler(0)]]) {\n"
             "  return source.sample(sourceSampler, in.uv);\n"
             "}\n";
        NSError *error = nil;
        id<MTLLibrary> library = nil;
        id<MTLLibrary> vertexLibrary = nil;
        id<MTLLibrary> fragmentLibrary = nil;
        const char *metallibPath = getenv("PVG_RENDER_METALLIB");
        if (metallibPath) {
            NSData *bytes = [NSData dataWithContentsOfFile:
                [NSString stringWithUTF8String:metallibPath]];
            dispatch_data_t data = bytes ? dispatch_data_create(
                bytes.bytes, bytes.length, NULL, ^{}) : nil;
            if (data) {
                library = [device newLibraryWithData:data error:&error];
            }
        } else {
            library = [device newLibraryWithSource:source
                                           options:nil
                                             error:&error];
        }
        if (!library) {
            fprintf(stderr, "library creation failed: %s\n",
                    error.description.UTF8String);
            return 2;
        }

        vertexLibrary = library;
        fragmentLibrary = library;
        const char *vertexMetallibPath =
            getenv("PVG_RENDER_VERTEX_METALLIB");
        const char *fragmentMetallibPath =
            getenv("PVG_RENDER_FRAGMENT_METALLIB");
        const char *separatePaths[2] = {
            vertexMetallibPath, fragmentMetallibPath
        };
        __strong id<MTLLibrary> *separateLibraries[2] = {
            &vertexLibrary, &fragmentLibrary
        };
        for (NSUInteger index = 0; index < 2; ++index) {
            if (!separatePaths[index])
                continue;
            NSData *bytes = [NSData dataWithContentsOfFile:
                [NSString stringWithUTF8String:separatePaths[index]]];
            dispatch_data_t data = bytes ? dispatch_data_create(
                bytes.bytes, bytes.length, NULL, ^{}) : nil;
            id<MTLLibrary> separate = data
                ? [device newLibraryWithData:data error:&error] : nil;
            if (!separate) {
                fprintf(stderr, "separate metallib creation failed: %s\n",
                        error.description.UTF8String);
                return 2;
            }
            *separateLibraries[index] = separate;
        }

        const char *vertexFunctionValue =
            getenv("PVG_RENDER_VERTEX_FUNCTION");
        const char *fragmentFunctionValue =
            getenv("PVG_RENDER_FRAGMENT_FUNCTION");
        NSString *vertexFunctionName = vertexFunctionValue
            ? [NSString stringWithUTF8String:vertexFunctionValue]
            : (useWindowServerCopy ? @"ViewportToNDC"
                                   : (useCompositor
                                      ? (useCompositorVertexBuffer
                                         ? @"pvgCompositorVertex"
                                         : @"pvgSampleVertex")
                                      : @"pvgVertex"));
        NSString *fragmentFunctionName = fragmentFunctionValue
            ? [NSString stringWithUTF8String:fragmentFunctionValue]
            : (useWindowServerCopy ? @"TextureCopy"
                                   : (useCompositor
                                      ? @"pvgCompositorFragment"
                                      : @"pvgFragment"));
        id<MTLFunction> vertexFunction =
            [vertexLibrary newFunctionWithName:vertexFunctionName];
        id<MTLFunction> fragmentFunction =
            [fragmentLibrary newFunctionWithName:fragmentFunctionName];
        if (getenv("PVG_INSPECT_FUNCTION_DATA")) {
            inspectFunctionData(vertexFunction);
            inspectFunctionData(fragmentFunction);
            return 0;
        }

        bool useIOSurface = getenv("PVG_RENDER_IOSURFACE") != NULL;
        bool partialDamage = getenv("PVG_RENDER_PARTIAL_DAMAGE") != NULL;
        if (partialDamage && !useIOSurface) {
            fprintf(stderr, "partial-damage test requires PVG_RENDER_IOSURFACE\n");
            return 64;
        }
        const char *pixelFormatValue = getenv("PVG_RENDER_PIXEL_FORMAT");
        MTLPixelFormat pixelFormat = pixelFormatValue
            ? (MTLPixelFormat)strtoull(pixelFormatValue, NULL, 0)
            : (useIOSurface ? MTLPixelFormatBGRA8Unorm
                            : MTLPixelFormatRGBA8Unorm);

        MTLRenderPipelineDescriptor *pipelineDescriptor =
            [[MTLRenderPipelineDescriptor alloc] init];
        pipelineDescriptor.vertexFunction = vertexFunction;
        pipelineDescriptor.fragmentFunction = fragmentFunction;
        pipelineDescriptor.colorAttachments[0].pixelFormat = pixelFormat;
        const char *concurrencyValue = getenv("PVG_PIPELINE_CONCURRENCY");
        if (concurrencyValue) {
            NSUInteger count = strtoull(concurrencyValue, NULL, 0);
            __block int failures = 0;
            dispatch_apply(count,
                           dispatch_get_global_queue(
                               DISPATCH_QUEUE_PRIORITY_DEFAULT, 0),
                           ^(size_t index) {
                @autoreleasepool {
                    MTLRenderPipelineDescriptor *descriptor =
                        [pipelineDescriptor copy];
                    NSError *pipelineError = nil;
                    id<MTLRenderPipelineState> concurrentPipeline =
                        [device newRenderPipelineStateWithDescriptor:descriptor
                                                               error:&pipelineError];
                    if (!concurrentPipeline) {
                        __sync_add_and_fetch(&failures, 1);
                        fprintf(stderr, "concurrent pipeline %zu failed: %s\n",
                                index, pipelineError.description.UTF8String);
                    }
                }
            });
            if (failures)
                return 3;
            printf("PASS concurrent pipelines device=%s count=%lu\n",
                   device.name.UTF8String, (unsigned long)count);
            return 0;
        }
        __strong id<MTLRenderPipelineState> compositorPipelines[3] = {
            nil, nil, nil
        };
        id<MTLRenderPipelineState> pipeline = nil;
        NSUInteger pipelineCount = useCompositor ? compositorTextureCount : 1;
        for (NSUInteger index = 0; index < pipelineCount; ++index) {
            id<MTLRenderPipelineState> candidate =
                [device newRenderPipelineStateWithDescriptor:pipelineDescriptor
                                                        error:&error];
            if (useCompositor)
                compositorPipelines[index] = candidate;
            else
                pipeline = candidate;
            if (!candidate)
                break;
        }
        if (useCompositor)
            pipeline = compositorPipelines[0];
        if (!pipeline) {
            fprintf(stderr, "pipeline creation failed: %s\n",
                    error.description.UTF8String);
            return 3;
        }

        const char *widthValue = getenv("PVG_RENDER_WIDTH");
        const char *heightValue = getenv("PVG_RENDER_HEIGHT");
        NSUInteger width = widthValue
            ? (NSUInteger)strtoull(widthValue, NULL, 0) : 16;
        NSUInteger height = heightValue
            ? (NSUInteger)strtoull(heightValue, NULL, 0) : 16;

        MTLTextureDescriptor *textureDescriptor =
            [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:pixelFormat
                                             width:width
                                            height:height
                                         mipmapped:NO];
        const char *textureType = getenv("PVG_RENDER_TEXTURE_TYPE");
        if (textureType)
            textureDescriptor.textureType =
                (MTLTextureType)strtoull(textureType, NULL, 0);
        const char *arrayLength = getenv("PVG_RENDER_ARRAY_LENGTH");
        if (arrayLength)
            textureDescriptor.arrayLength =
                (NSUInteger)strtoull(arrayLength, NULL, 0);
        const char *textureUsage = getenv("PVG_RENDER_TEXTURE_USAGE");
        textureDescriptor.usage = textureUsage
            ? (MTLTextureUsage)strtoull(textureUsage, NULL, 0)
            : MTLTextureUsageRenderTarget;
        const char *storageMode = getenv("PVG_RENDER_STORAGE_MODE");
        textureDescriptor.storageMode = storageMode
            ? (MTLStorageMode)strtoull(storageMode, NULL, 0)
            : (getenv("PVG_RENDER_PRIVATE_TEXTURE")
                ? MTLStorageModePrivate : MTLStorageModeShared);
        IOSurfaceRef surface = NULL;
        size_t surfaceBytesPerRow = width * 4;
        id<MTLBuffer> textureBacking = nil;
        id<MTLTexture> baseTexture = nil;
        id<MTLTexture> texture = nil;
        if (useIOSurface) {
            const size_t bytesPerElement = 4;
            const char *bytesPerRowValue =
                getenv("PVG_RENDER_BYTES_PER_ROW");
            surfaceBytesPerRow = bytesPerRowValue
                ? (size_t)strtoull(bytesPerRowValue, NULL, 0)
                : width * bytesPerElement;
            if (surfaceBytesPerRow < width * bytesPerElement) {
                fprintf(stderr, "bytes-per-row is smaller than the image\n");
                return 64;
            }
            NSDictionary *properties = @{
                (NSString *)kIOSurfaceWidth: @(width),
                (NSString *)kIOSurfaceHeight: @(height),
                (NSString *)kIOSurfaceBytesPerElement: @(bytesPerElement),
                (NSString *)kIOSurfaceBytesPerRow: @(surfaceBytesPerRow),
                (NSString *)kIOSurfaceAllocSize: @(surfaceBytesPerRow * height),
                (NSString *)kIOSurfacePixelFormat: @((uint32_t)'BGRA'),
            };
            surface = IOSurfaceCreate((CFDictionaryRef)properties);
            if (surface) {
                texture = [device newTextureWithDescriptor:textureDescriptor
                                                 iosurface:surface
                                                     plane:0];
            }
        } else if (getenv("PVG_RENDER_BUFFER_BACKED_TEXTURE")) {
            NSUInteger bytesPerRow = (width * 4 + 255) & ~((NSUInteger)255);
            textureBacking = [device newBufferWithLength:bytesPerRow * height
                                                  options:
                    MTLResourceStorageModeShared];
            if (textureBacking) {
                texture = [textureBacking newTextureWithDescriptor:
                    textureDescriptor offset:0 bytesPerRow:bytesPerRow];
            }
        } else {
            texture = [device newTextureWithDescriptor:textureDescriptor];
        }
        if (texture && getenv("PVG_RENDER_TEXTURE_VIEW")) {
            baseTexture = texture;
            texture = [baseTexture newTextureViewWithPixelFormat:pixelFormat];
        }
        if (partialDamage) {
            if (width < 24 || height < 24) {
                fprintf(stderr, "partial-damage test requires at least 24x24\n");
                return 64;
            }
            if (IOSurfaceLock(surface, 0, NULL) != kIOReturnSuccess) {
                fprintf(stderr, "failed to lock IOSurface for initialization\n");
                return 4;
            }
            uint8_t *base = IOSurfaceGetBaseAddress(surface);
            for (NSUInteger y = 0; y < height; ++y) {
                uint8_t *row = base + y * surfaceBytesPerRow;
                memset(row, 0xa5, surfaceBytesPerRow);
                for (NSUInteger x = 0; x < width; ++x) {
                    row[x * 4 + 0] = 16;
                    row[x * 4 + 1] = 32;
                    row[x * 4 + 2] = 48;
                    row[x * 4 + 3] = 255;
                }
            }
            IOSurfaceUnlock(surface, 0, NULL);
        }
        const NSUInteger readbackWidth = getenv("PVG_RENDER_FULL_READBACK")
            ? width : (partialDamage ? 32 : 16);
        const NSUInteger readbackHeight = getenv("PVG_RENDER_FULL_READBACK")
            ? height : (partialDamage ? 32 : 16);
        id<MTLBuffer> readback = [device newBufferWithLength:
                readbackWidth * readbackHeight * 4
                                                     options:
                MTLResourceStorageModeShared];
        if (!texture || !readback) {
            fprintf(stderr, "texture/readback allocation failed: texture=%s "
                    "readback=%s storage=%lu iosurface=%s\n",
                    texture ? "yes" : "no", readback ? "yes" : "no",
                    (unsigned long)textureDescriptor.storageMode,
                    surface ? "yes" : "no");
            if (surface)
                CFRelease(surface);
            return 4;
        }
        if (getenv("PVG_RENDER_ALLOC_ONLY")) {
            printf("PASS allocation-only device=%s size=%lux%lu usage=%lu "
                   "storage=%lu iosurface=%s\n",
                   device.name.UTF8String,
                   (unsigned long)width, (unsigned long)height,
                   (unsigned long)textureDescriptor.usage,
                   (unsigned long)textureDescriptor.storageMode,
                   surface ? "yes" : "no");
            if (surface)
                CFRelease(surface);
            return 0;
        }

        __strong id<MTLTexture> compositorTextures[3] = {nil, nil, nil};
        id<MTLHeap> compositorHeap = nil;
        id<MTLBuffer> compositorBuffer = nil;
        id<MTLSamplerState> compositorSampler = nil;
        if (useCompositor) {
            const NSUInteger sourceWidths[3] = {234, 404, 84};
            const NSUInteger sourceHeights[3] = {18, 14, 103};
            __strong MTLTextureDescriptor *sourceDescriptors[3] = {
                nil, nil, nil
            };
            NSUInteger heapSize = 0;
            NSUInteger heapAlignment = 1;
            for (NSUInteger index = 0; index < compositorTextureCount;
                 ++index) {
                MTLTextureDescriptor *descriptor = [MTLTextureDescriptor
                    texture2DDescriptorWithPixelFormat:pixelFormat
                                                 width:sourceWidths[index]
                                                height:sourceHeights[index]
                                             mipmapped:NO];
                descriptor.usage = MTLTextureUsageShaderRead;
                descriptor.storageMode = MTLStorageModeShared;
                sourceDescriptors[index] = descriptor;
                if (getenv("PVG_RENDER_COMPOSITOR_HEAP")) {
                    MTLSizeAndAlign sizeAndAlign =
                        [device heapTextureSizeAndAlignWithDescriptor:descriptor];
                    heapAlignment = MAX(heapAlignment, sizeAndAlign.align);
                    heapSize = (heapSize + sizeAndAlign.align - 1) &
                        ~(sizeAndAlign.align - 1);
                    heapSize += sizeAndAlign.size;
                }
            }
            if (getenv("PVG_RENDER_COMPOSITOR_HEAP")) {
                MTLHeapDescriptor *heapDescriptor =
                    [[MTLHeapDescriptor alloc] init];
                heapDescriptor.storageMode = MTLStorageModeShared;
                heapDescriptor.size = (heapSize + heapAlignment - 1) &
                    ~(heapAlignment - 1);
                compositorHeap = [device newHeapWithDescriptor:heapDescriptor];
            }
            for (NSUInteger index = 0; index < compositorTextureCount;
                 ++index) {
                compositorTextures[index] = compositorHeap
                    ? [compositorHeap newTextureWithDescriptor:
                        sourceDescriptors[index]]
                    : [device newTextureWithDescriptor:sourceDescriptors[index]];
                NSUInteger bytesPerRow = sourceWidths[index] * 4;
                NSMutableData *sourceBytes = [NSMutableData
                    dataWithLength:bytesPerRow * sourceHeights[index]];
                uint8_t *pixelBytes = sourceBytes.mutableBytes;
                for (NSUInteger pixel = 0;
                     pixel < sourceWidths[index] * sourceHeights[index];
                     ++pixel) {
                    pixelBytes[pixel * 4 + 0] =
                        pixelFormat == MTLPixelFormatBGRA8Unorm ? 192 : 64;
                    pixelBytes[pixel * 4 + 1] = 128;
                    pixelBytes[pixel * 4 + 2] =
                        pixelFormat == MTLPixelFormatBGRA8Unorm ? 64 : 192;
                    pixelBytes[pixel * 4 + 3] = 255;
                }
                if (!getenv("PVG_RENDER_COMPOSITOR_NO_UPLOAD")) {
                    [compositorTextures[index]
                        replaceRegion:MTLRegionMake2D(0, 0,
                                                      sourceWidths[index],
                                                      sourceHeights[index])
                          mipmapLevel:0
                            withBytes:sourceBytes.bytes
                          bytesPerRow:bytesPerRow];
                }
            }
            if (useCompositorVertexBuffer) {
                compositorBuffer = [device newBufferWithLength:0x40000
                                                        options:
                        MTLResourceStorageModeShared];
                uint16_t indices[3] = {0, 1, 2};
                memcpy(compositorBuffer.contents, indices, sizeof(indices));
                const float vertices[12] = {
                    -1.0f, -1.0f, 0.0f, 1.0f,
                     3.0f, -1.0f, 2.0f, 1.0f,
                    -1.0f,  3.0f, 0.0f, -1.0f,
                };
                memcpy((uint8_t *)compositorBuffer.contents + 0x100,
                       vertices, sizeof(vertices));
            }
            MTLSamplerDescriptor *samplerDescriptor =
                [[MTLSamplerDescriptor alloc] init];
            samplerDescriptor.minFilter = MTLSamplerMinMagFilterNearest;
            samplerDescriptor.magFilter = MTLSamplerMinMagFilterNearest;
            samplerDescriptor.sAddressMode = MTLSamplerAddressModeClampToEdge;
            samplerDescriptor.tAddressMode = MTLSamplerAddressModeClampToEdge;
            compositorSampler = [device newSamplerStateWithDescriptor:
                samplerDescriptor];
            BOOL missingTexture = NO;
            for (NSUInteger index = 0; index < compositorTextureCount;
                 ++index)
                missingTexture |= compositorTextures[index] == nil;
            if (missingTexture ||
                (useCompositorVertexBuffer && !compositorBuffer) ||
                !compositorSampler) {
                fprintf(stderr, "compositor resource allocation failed\n");
                if (surface)
                    CFRelease(surface);
                return 7;
            }
        }

        IOSurfaceRef copySourceSurface = NULL;
        id<MTLTexture> copySourceTexture = nil;
        id<MTLBuffer> copyVertexBuffer = nil;
        if (useWindowServerCopy) {
            const char *bytesPerRowValue =
                getenv("PVG_RENDER_BYTES_PER_ROW");
            const size_t bytesPerRow = bytesPerRowValue
                ? (size_t)strtoull(bytesPerRowValue, NULL, 0)
                : width * 4;
            NSDictionary *properties = @{
                (NSString *)kIOSurfaceWidth: @(width),
                (NSString *)kIOSurfaceHeight: @(height),
                (NSString *)kIOSurfaceBytesPerElement: @4,
                (NSString *)kIOSurfaceBytesPerRow: @(bytesPerRow),
                (NSString *)kIOSurfaceAllocSize: @(bytesPerRow * height),
                (NSString *)kIOSurfacePixelFormat: @((uint32_t)'BGRA'),
            };
            copySourceSurface = IOSurfaceCreate((CFDictionaryRef)properties);
            MTLTextureDescriptor *sourceDescriptor = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:pixelFormat
                                             width:width
                                            height:height
                                         mipmapped:NO];
            sourceDescriptor.storageMode = MTLStorageModeShared;
            sourceDescriptor.usage = MTLTextureUsageUnknown;
            if (copySourceSurface)
                copySourceTexture = [device
                    newTextureWithDescriptor:sourceDescriptor
                                   iosurface:copySourceSurface
                                       plane:0];
            copyVertexBuffer = [device newBufferWithLength:0x20000
                                                   options:
                MTLResourceStorageModeShared];
            if (!copySourceTexture || !copyVertexBuffer || !surface) {
                fprintf(stderr,
                        "WindowServer copy resources failed source=%s "
                        "buffer=%s targetSurface=%s\n",
                        copySourceTexture ? "yes" : "no",
                        copyVertexBuffer ? "yes" : "no",
                        surface ? "yes" : "no");
                return 7;
            }
            NSMutableData *sourceBytes = [NSMutableData
                dataWithLength:bytesPerRow * height];
            uint8_t *pixels = sourceBytes.mutableBytes;
            for (NSUInteger row = 0; row < height; ++row) {
                for (NSUInteger column = 0; column < width; ++column) {
                    uint8_t *pixel = pixels + row * bytesPerRow + column * 4;
                    pixel[0] = 192;
                    pixel[1] = 128;
                    pixel[2] = 64;
                    pixel[3] = 255;
                }
            }
            [copySourceTexture replaceRegion:MTLRegionMake2D(0, 0, width,
                                                             height)
                                  mipmapLevel:0
                                    withBytes:sourceBytes.bytes
                                  bytesPerRow:bytesPerRow];
            const float positions[12] = {
                -1.0f, -1.0f,  1.0f, -1.0f, -1.0f,  1.0f,
                 1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f,
            };
            const float coordinates[12] = {
                0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f,
                1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f,
            };
            const float matrix[16] = {
                1, 0, 0, 0,
                0, 1, 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1,
            };
            memcpy((uint8_t *)copyVertexBuffer.contents + 0x100,
                   positions, sizeof(positions));
            memcpy((uint8_t *)copyVertexBuffer.contents + 0x300,
                   coordinates, sizeof(coordinates));
            memcpy((uint8_t *)copyVertexBuffer.contents + 0x500,
                   matrix, sizeof(matrix));
        }

        MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor
            renderPassDescriptor];
        pass.colorAttachments[0].texture = texture;
        const char *loadActionValue = getenv("PVG_RENDER_LOAD_ACTION");
        pass.colorAttachments[0].loadAction = partialDamage
            ? MTLLoadActionLoad
            : loadActionValue
            ? (MTLLoadAction)strtoull(loadActionValue, NULL, 0)
            : MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);

        id<MTLCommandQueue> queue = [device newCommandQueue];
        id<MTLCommandBuffer> command = [queue commandBuffer];
        id<MTLRenderCommandEncoder> encoder =
            [command renderCommandEncoderWithDescriptor:pass];
        if (partialDamage)
            [encoder setScissorRect:(MTLScissorRect){8, 8, 16, 16}];
        if (!getenv("PVG_RENDER_NO_DRAW")) {
            if (useWindowServerCopy) {
                [encoder setRenderPipelineState:pipeline];
                [encoder setVertexBuffer:copyVertexBuffer offset:0x000
                                 atIndex:0];
                [encoder setVertexBuffer:copyVertexBuffer offset:0x100
                                 atIndex:0];
                [encoder setVertexBuffer:copyVertexBuffer offset:0x200
                                 atIndex:1];
                [encoder setVertexBuffer:copyVertexBuffer offset:0x300
                                 atIndex:1];
                [encoder setVertexBuffer:copyVertexBuffer offset:0x400
                                 atIndex:2];
                [encoder setVertexBuffer:copyVertexBuffer offset:0x500
                                 atIndex:2];
                [encoder setFragmentTexture:copySourceTexture atIndex:0];
                [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                            vertexStart:0 vertexCount:6];
            } else if (useCompositor) {
                if (useCompositorVertexBuffer)
                    [encoder setVertexBuffer:compositorBuffer offset:0x100
                                     atIndex:0];
                [encoder setFragmentSamplerState:compositorSampler atIndex:0];
                for (NSUInteger index = 0; index < compositorTextureCount;
                     ++index) {
                    [encoder setRenderPipelineState:compositorPipelines[index]];
                    [encoder setFragmentTexture:compositorTextures[index]
                                        atIndex:3];
                    if (useCompositorVertexBuffer) {
                        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                            indexCount:3
                                             indexType:MTLIndexTypeUInt16
                                           indexBuffer:compositorBuffer
                                     indexBufferOffset:0];
                    } else {
                        [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                                    vertexStart:0
                                    vertexCount:3];
                    }
                }
            } else {
                [encoder setRenderPipelineState:pipeline];
                [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                            vertexStart:0
                            vertexCount:3];
            }
        }
        [encoder endEncoding];
        if (!getenv("PVG_RENDER_NO_BLIT")) {
            id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
            [blit copyFromTexture:texture
                     sourceSlice:0
                     sourceLevel:0
                    sourceOrigin:MTLOriginMake(0, 0, 0)
                    sourceSize:MTLSizeMake(readbackWidth, readbackHeight, 1)
                        toBuffer:readback
               destinationOffset:0
          destinationBytesPerRow:readbackWidth * 4
        destinationBytesPerImage:readbackWidth * readbackHeight * 4];
            [blit endEncoding];
        }
        const char *delayValue = getenv("PVG_RENDER_PRECOMMIT_DELAY_US");
        if (delayValue)
            usleep((useconds_t)strtoull(delayValue, NULL, 0));
        [command commit];
        [command waitUntilCompleted];
        if (command.status != MTLCommandBufferStatusCompleted) {
            fprintf(stderr, "command failed: %s\n",
                    command.error.description.UTF8String);
            return 5;
        }

        if (getenv("PVG_RENDER_NO_BLIT")) {
            printf("PASS command-only device=%s size=%lux%lu iosurface=%s\n",
                   device.name.UTF8String, (unsigned long)width,
                   (unsigned long)height, surface ? "yes" : "no");
            if (surface)
                CFRelease(surface);
            return 0;
        }

        uint8_t *pixels = readback.contents;
        uint8_t expectedRed = pixelFormat == MTLPixelFormatBGRA8Unorm ? 192 : 64;
        uint8_t expectedBlue = pixelFormat == MTLPixelFormatBGRA8Unorm ? 64 : 192;
        if (partialDamage) {
            uint8_t *inside = pixels + (8 * readbackWidth + 8) * 4;
            // Check every read-back pixel, not just two samples: page-sized
            // holes outside the scissor could escape the original two samples.
            for (NSUInteger y = 0; y < readbackHeight; ++y) {
                for (NSUInteger x = 0; x < readbackWidth; ++x) {
                    BOOL drawn = x >= 8 && x < 24 && y >= 8 && y < 24;
                    uint8_t *p = pixels + (y * readbackWidth + x) * 4;
                    int r = drawn ? expectedRed : 16;
                    int g = drawn ? 128 : 32;
                    int b = drawn ? expectedBlue : 48;
                    int tolerance = drawn ? 2 : 0;
                    if (abs((int)p[0] - r) > tolerance ||
                        abs((int)p[1] - g) > tolerance ||
                        abs((int)p[2] - b) > tolerance || p[3] != 255) {
                        fprintf(stderr, "partial-damage pixel %lu,%lu drawn=%d mismatch: %u,%u,%u,%u\n",
                            (unsigned long)x, (unsigned long)y, drawn,
                            p[0], p[1], p[2], p[3]);
                        return 6;
                    }
                }
            }
            if (pixels[0] != 16 || pixels[1] != 32 || pixels[2] != 48 ||
                pixels[3] != 255 ||
                inside[0] + 2 < expectedRed || inside[0] > expectedRed + 2 ||
                inside[1] < 126 || inside[1] > 130 ||
                inside[2] + 2 < expectedBlue || inside[2] > expectedBlue + 2 ||
                inside[3] != 255) {
                fprintf(stderr,
                        "partial-damage mismatch: outside=%u,%u,%u,%u "
                        "inside=%u,%u,%u,%u\n",
                        pixels[0], pixels[1], pixels[2], pixels[3],
                        inside[0], inside[1], inside[2], inside[3]);
                return 6;
            }
            printf("PASS partial-damage device=%s size=%lux%lu "
                   "outside=%u,%u,%u,%u inside=%u,%u,%u,%u "
                   "bytes-per-row=%zu\n",
                   device.name.UTF8String, (unsigned long)width,
                   (unsigned long)height,
                   pixels[0], pixels[1], pixels[2], pixels[3],
                   inside[0], inside[1], inside[2], inside[3],
                   surfaceBytesPerRow);
            if (surface)
                CFRelease(surface);
            return 0;
        }
        for (NSUInteger pixel = 0; pixel < readbackWidth * readbackHeight; pixel++) {
            pixels = (uint8_t *)readback.contents + pixel * 4;
            if (pixels[0] + 2 < expectedRed || pixels[0] > expectedRed + 2 ||
                pixels[1] < 126 || pixels[1] > 130 ||
                pixels[2] + 2 < expectedBlue || pixels[2] > expectedBlue + 2 ||
                pixels[3] != 255) {
                fprintf(stderr, "pixel %lu mismatch: %u,%u,%u,%u\n",
                        (unsigned long)pixel, pixels[0], pixels[1],
                        pixels[2], pixels[3]);
                return 6;
            }
        }
        printf("PASS device=%s size=%lux%lu pixel=%u,%u,%u,%u iosurface=%s\n",
               device.name.UTF8String, (unsigned long)width,
               (unsigned long)height, pixels[0], pixels[1], pixels[2],
               pixels[3], surface ? "yes" : "no");
        if (surface)
            CFRelease(surface);
    }
    return 0;
}
