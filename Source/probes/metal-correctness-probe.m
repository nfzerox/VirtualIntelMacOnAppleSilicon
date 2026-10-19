#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <objc/message.h>
#import <objc/runtime.h>

#include <dlfcn.h>
#include <CommonCrypto/CommonDigest.h>
#include <stdio.h>
#include <unistd.h>

static const NSUInteger elementCount = 1024;
static uint8_t downleveledBitcodeHash[CC_SHA256_DIGEST_LENGTH];
static BOOL downleveledBitcodeHashValid;

typedef void *LLVMMemoryBufferRef;
typedef void *LLVMModuleRef;

static NSUInteger findBytes(const uint8_t *bytes, NSUInteger length,
                            const void *needle, NSUInteger needleLength) {
    for (NSUInteger offset = 0; offset + needleLength <= length; ++offset) {
        if (memcmp(bytes + offset, needle, needleLength) == 0)
            return offset;
    }
    return NSNotFound;
}

static BOOL downlevelLibrary(id<MTLLibrary> library, NSData **compiledLibrary) {
    SEL selector = sel_registerName("libraryData");
    if (![library respondsToSelector:selector]) return NO;
    void *(*sendPointer)(id, SEL) = (void *(*)(id, SEL))objc_msgSend;
    uint8_t *libraryData = sendPointer(library, selector);
    const uint8_t *mappedBytes = *(const uint8_t **)(libraryData + 0x48);
    NSUInteger mappedLength = *(NSUInteger *)(libraryData + 0x50);

    const uint8_t magic[] = {'B', 'C', 0xc0, 0xde};
    NSUInteger bitcodeOffset = findBytes(mappedBytes, mappedLength, magic,
                                         sizeof(magic));
    if (bitcodeOffset == NSNotFound || bitcodeOffset < 20) return NO;
    NSUInteger wrapperOffset = bitcodeOffset - 20;
    uint32_t wrapperMagic = 0;
    uint32_t oldBitcodeLength = 0;
    memcpy(&wrapperMagic, mappedBytes + wrapperOffset, 4);
    memcpy(&oldBitcodeLength, mappedBytes + wrapperOffset + 12, 4);
    if (wrapperMagic != UINT32_C(0x0b17c0de) ||
        oldBitcodeLength > mappedLength - bitcodeOffset)
        return NO;

    void *llvmImage = dlopen(
        "/System/Library/PrivateFrameworks/GPUCompiler.framework/Versions/"
        "31001/Libraries/PVGLLVMCompat.dylib", RTLD_NOW | RTLD_LOCAL);
    void *gpuImage = dlopen(
        "/System/Library/PrivateFrameworks/GPUCompiler.framework/Versions/"
        "31001/Libraries/PVGGPUCompilerCompat.dylib", RTLD_NOW | RTLD_LOCAL);
    if (!llvmImage || !gpuImage) return NO;

#define PVG_DLSYM(handle, name) (__typeof__(&name))dlsym((handle), #name)
    LLVMMemoryBufferRef (*createBuffer)(const char *, size_t, const char *) =
        dlsym(llvmImage, "LLVMCreateMemoryBufferWithMemoryRangeCopy");
    int (*parseBitcode)(LLVMMemoryBufferRef, LLVMModuleRef *) =
        dlsym(llvmImage, "LLVMParseBitcode2");
    void (*setTarget)(LLVMModuleRef, const char *) =
        dlsym(llvmImage, "LLVMSetTarget");
    BOOL (*downgrade)(LLVMModuleRef, uint64_t, uint32_t) =
        dlsym(gpuImage, "MTLDowngradeAIRModule");
    LLVMMemoryBufferRef (*writeBitcode)(LLVMModuleRef, unsigned) =
        dlsym(gpuImage, "MTLWriteAIRBitcodeToMemoryBuffer");
    const char *(*getBufferStart)(LLVMMemoryBufferRef) =
        dlsym(llvmImage, "LLVMGetBufferStart");
    size_t (*getBufferSize)(LLVMMemoryBufferRef) =
        dlsym(llvmImage, "LLVMGetBufferSize");
#undef PVG_DLSYM
    if (!createBuffer || !parseBitcode || !setTarget || !downgrade ||
        !writeBitcode || !getBufferStart || !getBufferSize)
        return NO;

    LLVMMemoryBufferRef input = createBuffer(
        (const char *)mappedBytes + bitcodeOffset, oldBitcodeLength, "PVG AIR");
    LLVMModuleRef module = NULL;
    if (parseBitcode(input, &module)) return NO;
    setTarget(module, "air64-apple-macosx10.15.0");
    if (!downgrade(module, UINT64_C(0x200000002), 0)) return NO;
    LLVMMemoryBufferRef output = writeBitcode(module, 0);
    if (!output) return NO;

    void *plugin = dlopen(
        "/System/Library/Extensions/AppleParavirtGPUMetal.bundle/Contents/"
        "Resources/libAppleParavirtCompilerPlugin.dylib", RTLD_NOW | RTLD_LOCAL);
    void *(*createCompiler)(void) = plugin ?
        dlsym(plugin, "MTLCompilerCreate") : NULL;
    int (*buildRequest)(void *, const void *, size_t, unsigned, LLVMModuleRef,
                        const void **, size_t *, void **, size_t *, void **,
                        size_t *, const char **) = plugin ?
        dlsym(plugin, "MTLCompilerBuildRequestWithOptions") : NULL;
    void (*releaseReply)(void *) = plugin ?
        dlsym(plugin, "MTLCompilerReleaseReply") : NULL;
    void (*deleteCompiler)(void *) = plugin ?
        dlsym(plugin, "MTLCompilerDelete") : NULL;
    if (createCompiler && buildRequest && releaseReply && deleteCompiler) {
        void *compiler = createCompiler();
        const void *data = NULL;
        size_t dataLength = 0;
        void *unused1 = NULL, *unused2 = NULL;
        size_t unusedLength1 = 0, unusedLength2 = 0;
        const char *compilerError = NULL;
        int result = buildRequest(
            compiler, NULL, 0, 0, module, &data, &dataLength,
            &unused1, &unusedLength1, &unused2, &unusedLength2,
            &compilerError);
        fprintf(stderr, "direct PVG compile result=%d bytes=%lu error=%s\n",
                result, (unsigned long)dataLength,
                compilerError ? compilerError : "none");
        if (result == 0 && data && dataLength && compiledLibrary)
            *compiledLibrary = [NSData dataWithBytes:data length:dataLength];
        releaseReply(compiler);
        deleteCompiler(compiler);
    }

    size_t newBitcodeLength = getBufferSize(output);
    CC_SHA256(getBufferStart(output), (CC_LONG)newBitcodeLength,
              downleveledBitcodeHash);
    downleveledBitcodeHashValid = YES;
    size_t oldWrapperLength = (20 + oldBitcodeLength + 15) & ~(size_t)15;
    size_t newWrapperLength = (20 + newBitcodeLength + 15) & ~(size_t)15;
    size_t suffixOffset = wrapperOffset + oldWrapperLength;
    if (suffixOffset > mappedLength || newBitcodeLength > UINT32_MAX ||
        newWrapperLength > UINT32_MAX)
        return NO;
    size_t newMappedLength = mappedLength - oldWrapperLength + newWrapperLength;
    uint8_t *writableBytes = calloc(1, newMappedLength);
    if (!writableBytes) return NO;
    memcpy(writableBytes, mappedBytes, bitcodeOffset);
    memcpy(writableBytes + bitcodeOffset, getBufferStart(output),
           newBitcodeLength);
    memcpy(writableBytes + wrapperOffset + newWrapperLength,
           mappedBytes + suffixOffset, mappedLength - suffixOffset);
    uint32_t encodedBitcodeLength = (uint32_t)newBitcodeLength;
    uint32_t encodedWrapperLength = (uint32_t)newWrapperLength;
    memcpy(writableBytes + wrapperOffset + 12, &encodedBitcodeLength, 4);
    memcpy(writableBytes + wrapperOffset - 4, &encodedWrapperLength, 4);
    *(uint8_t **)(libraryData + 0x48) = writableBytes;
    *(NSUInteger *)(libraryData + 0x50) = newMappedLength;
    fprintf(stderr, "downleveled AIR bytes=%u->%u library=%lu->%lu\n",
            oldBitcodeLength, encodedBitcodeLength,
            (unsigned long)mappedLength, (unsigned long)newMappedLength);
    return YES;
}

int main(void) {
    @autoreleasepool {
        NSArray<id<MTLDevice>> *devices = MTLCopyAllDevices();
        id<MTLDevice> device = devices.firstObject;
        if (!device) {
            fprintf(stderr, "no Metal devices\n");
            return 1;
        }

        uint32_t nonce = arc4random() & 0xffff;
        NSString *source = [NSString stringWithFormat:
            @"#include <metal_stdlib>\n"
             "using namespace metal;\n"
             "kernel void multiply(device const uint *a [[buffer(0)]],\n"
             "                     device const uint *b [[buffer(1)]],\n"
             "                     device uint *out [[buffer(2)]],\n"
             "                     uint i [[thread_position_in_grid]]) {\n"
             "  out[i] = a[i] * b[i] + 7 + %u;\n"
             "}\n", nonce];
        NSError *error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:source
                                                     options:nil
                                                       error:&error];
        if (!library) {
            fprintf(stderr, "library creation failed: %s\n",
                    error.localizedDescription.UTF8String);
            return 2;
        }

        NSData *compiledLibrary = nil;
        if (!downlevelLibrary(library, &compiledLibrary)) {
            fprintf(stderr, "warning: failed to downlevel returned AIR\n");
        } else {
            SEL contentsSelector = sel_registerName("libraryDataContents");
            id (*sendObject)(id, SEL) = (id (*)(id, SEL))objc_msgSend;
            NSData *contents = sendObject(library, contentsSelector);
            dispatch_data_t data = dispatch_data_create(
                contents.bytes, contents.length, NULL,
                ^{});
            id<MTLLibrary> reparsed = [device newLibraryWithData:data
                                                           error:&error];
            if (reparsed) {
                library = reparsed;
                fprintf(stderr, "reparsed downleveled AIR library\n");
            } else {
                fprintf(stderr, "downleveled library reparse failed: %s\n",
                        error.localizedDescription.UTF8String);
            }
        }
        if (compiledLibrary) {
            dispatch_data_t data = dispatch_data_create(
                compiledLibrary.bytes, compiledLibrary.length, NULL, ^{});
            id<MTLLibrary> executableLibrary =
                [device newLibraryWithData:data error:&error];
            if (executableLibrary) {
                library = executableLibrary;
                downleveledBitcodeHashValid = NO;
                fprintf(stderr, "loaded direct PVG executable metallib\n");
            } else {
                fprintf(stderr, "direct PVG metallib load failed: %s\n",
                        error.description.UTF8String);
            }
        }
        SEL overrideSelector = sel_registerName("setOverrideTriple:");
        if (!compiledLibrary && [library respondsToSelector:overrideSelector]) {
            void (*sendOverride)(id, SEL, id) =
                (void (*)(id, SEL, id))objc_msgSend;
            sendOverride(library, overrideSelector,
                         @"air64-apple-macosx10.15.0");
            fprintf(stderr, "set Catalina AIR override triple\n");
        }

        const char *dumpPath = getenv("PVG_DUMP_METALLIB");
        if (dumpPath) {
            Class cls = object_getClass(library);
            unsigned int methodCount = 0;
            Method *methods = class_copyMethodList(cls, &methodCount);
            fprintf(stderr, "library class=%s methods=%u\n",
                    class_getName(cls), methodCount);
            for (unsigned int index = 0; index < methodCount; ++index) {
                const char *name = sel_getName(method_getName(methods[index]));
                fprintf(stderr, "library method: %s type=%s\n", name,
                        method_getTypeEncoding(methods[index]));
            }
            free(methods);

            SEL contentsSelector = sel_registerName("libraryDataContents");
            if ([library respondsToSelector:contentsSelector]) {
                id (*sendObject)(id, SEL) =
                    (id (*)(id, SEL))objc_msgSend;
                id contents = sendObject(library, contentsSelector);
                fprintf(stderr, "libraryDataContents class=%s\n",
                        contents ? object_getClassName(contents) : "nil");
                if ([contents isKindOfClass:[NSData class]]) {
                    BOOL wrote = [(NSData *)contents
                        writeToFile:[NSString stringWithUTF8String:dumpPath]
                        atomically:NO];
                    fprintf(stderr, "libraryDataContents bytes=%lu wrote=%s\n",
                            (unsigned long)[(NSData *)contents length],
                            wrote ? "yes" : "no");

                }
            }

            SEL selector = sel_registerName("serializeToURL:error:");
            if ([library respondsToSelector:selector]) {
                NSURL *url = [NSURL fileURLWithPath:
                    [NSString stringWithUTF8String:dumpPath]];
                NSError *dumpError = nil;
                BOOL (*send)(id, SEL, NSURL *, NSError **) =
                    (BOOL (*)(id, SEL, NSURL *, NSError **))objc_msgSend;
                BOOL ok = send(library, selector, url, &dumpError);
                fprintf(stderr, "serializeToURL=%s error=%s\n",
                        ok ? "yes" : "no",
                        dumpError.localizedDescription.UTF8String ?: "none");
            }
        }

        id<MTLFunction> function = [library newFunctionWithName:@"multiply"];
        SEL functionDataSelector = sel_registerName("functionData");
        if ([function respondsToSelector:functionDataSelector]) {
            void *(*sendPointer)(id, SEL) =
                (void *(*)(id, SEL))objc_msgSend;
            uint8_t *functionData = sendPointer(function,
                                                 functionDataSelector);
            fprintf(stderr, "function bitcode offset=%llu size=%llu\n",
                    *(unsigned long long *)(functionData + 0x00),
                    *(unsigned long long *)(functionData + 0x08));
            fprintf(stderr, "function AIR=%u.%u language=%u.%u\n",
                    *(uint16_t *)(functionData + 0x28),
                    *(uint16_t *)(functionData + 0x2a),
                    *(uint16_t *)(functionData + 0x2c),
                    *(uint16_t *)(functionData + 0x2e));
            if (*(uint16_t *)(functionData + 0x28) == 2 &&
                *(uint16_t *)(functionData + 0x2a) == 3) {
                *(uint16_t *)(functionData + 0x2a) = 2;
                fprintf(stderr, "synchronized cached AIR version to 2.2\n");
            }
            if (downleveledBitcodeHashValid) {
                memcpy(functionData + 0x30, downleveledBitcodeHash,
                       sizeof(downleveledBitcodeHash));
                fprintf(stderr, "synchronized cached AIR bitcode hash\n");
            }
        }
        id<MTLComputePipelineState> pipeline =
            [device newComputePipelineStateWithFunction:function error:&error];
        if (!pipeline) {
            fprintf(stderr, "pipeline creation failed: %s\nuserInfo=%s\n",
                    error.description.UTF8String,
                    error.userInfo.description.UTF8String);
            return 3;
        }

        NSUInteger byteCount = elementCount * sizeof(uint32_t);
        MTLResourceOptions options = MTLResourceStorageModeShared;
        id<MTLBuffer> inputA = [device newBufferWithLength:byteCount
                                                  options:options];
        id<MTLBuffer> inputB = [device newBufferWithLength:byteCount
                                                  options:options];
        id<MTLBuffer> output = [device newBufferWithLength:byteCount
                                                  options:options];
        if (!inputA || !inputB || !output) {
            fprintf(stderr, "buffer allocation failed\n");
            return 4;
        }

        uint32_t *a = inputA.contents;
        uint32_t *b = inputB.contents;
        uint32_t *result = output.contents;
        for (NSUInteger index = 0; index < elementCount; ++index) {
            a[index] = (uint32_t)(index * 3 + 1);
            b[index] = (uint32_t)(index ^ 0x155);
            result[index] = 0xdeadbeef;
        }

        id<MTLCommandQueue> queue = [device newCommandQueue];
        id<MTLCommandBuffer> command = [queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
        [encoder setComputePipelineState:pipeline];
        [encoder setBuffer:inputA offset:0 atIndex:0];
        [encoder setBuffer:inputB offset:0 atIndex:1];
        [encoder setBuffer:output offset:0 atIndex:2];
        MTLSize grid = MTLSizeMake(elementCount, 1, 1);
        NSUInteger width = pipeline.maxTotalThreadsPerThreadgroup;
        if (width > elementCount) width = elementCount;
        [encoder dispatchThreads:grid
           threadsPerThreadgroup:MTLSizeMake(width, 1, 1)];
        [encoder endEncoding];
        [command commit];
        [command waitUntilCompleted];
        if (command.status != MTLCommandBufferStatusCompleted) {
            fprintf(stderr, "command failed, status=%lu: %s\n",
                    (unsigned long)command.status,
                    command.error.localizedDescription.UTF8String);
            return 5;
        }

        for (NSUInteger index = 0; index < elementCount; ++index) {
            uint32_t expected = a[index] * b[index] + 7 + nonce;
            if (result[index] != expected) {
                fprintf(stderr,
                        "mismatch at %lu: got 0x%x expected 0x%x\n",
                        (unsigned long)index, result[index], expected);
                return 6;
            }
        }

        printf("PASS device=%s elements=%lu\n", device.name.UTF8String,
               (unsigned long)elementCount);
        return 0;
    }
}
