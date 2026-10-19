#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../compat/PVGAIRMetadata.h"

typedef void *LLVMMemoryBufferRef;
typedef void *LLVMModuleRef;

int main(int argc, char **argv) {
    const char *pluginOverride = getenv("PVG_COMPILER_PLUGIN_IMAGE");
    int pluginMode = pluginOverride && *pluginOverride;
    if ((pluginMode && argc != 3) || (!pluginMode && (argc < 2 || argc > 3))) {
        fprintf(stderr,
                "usage: %s library-data [rewritten-bitcode]\n"
                "       PVG_COMPILER_PLUGIN_IMAGE=... %s library-data metallib\n",
                argv[0], argv[0]);
        return 64;
    }
    FILE *file = fopen(argv[1], "rb");
    if (!file) return 66;
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    rewind(file);
    uint8_t *bytes = malloc((size_t)length);
    if (!bytes || fread(bytes, 1, (size_t)length, file) != (size_t)length)
        return 74;
    fclose(file);

    size_t offset = 0;
    const uint8_t magic[] = {'B', 'C', 0xc0, 0xde};
    while (offset + sizeof(magic) <= (size_t)length &&
           memcmp(bytes + offset, magic, sizeof(magic)) != 0)
        ++offset;
    if (offset + sizeof(magic) > (size_t)length) return 65;

    const char *llvmPath = getenv("PVG_LLVM_IMAGE");
    if (!llvmPath)
        llvmPath =
            "/System/Library/PrivateFrameworks/GPUCompiler.framework/"
            "Versions/31001/Libraries/PVGLLVMCompat.dylib";
    void *image = dlopen(llvmPath, RTLD_NOW | RTLD_LOCAL);
    if (!image) {
        fprintf(stderr, "dlopen: %s\n", dlerror());
        return 1;
    }
    const char *gpuPath = getenv("PVG_GPU_IMAGE");
    if (!gpuPath)
        gpuPath =
            "/System/Library/PrivateFrameworks/GPUCompiler.framework/"
            "Versions/31001/Libraries/PVGPluginGPUCompilerBridge.dylib";
    void *gpuImage = dlopen(gpuPath, RTLD_NOW | RTLD_LOCAL);
    LLVMMemoryBufferRef (*createBuffer)(const char *, size_t, const char *) =
        dlsym(image, "LLVMCreateMemoryBufferWithMemoryRangeCopy");
    int (*parseBitcode)(LLVMMemoryBufferRef, LLVMModuleRef *) =
        dlsym(image, "LLVMParseBitcode2");
    char *(*printModule)(LLVMModuleRef) = dlsym(image, "LLVMPrintModuleToString");
    void (*disposeMessage)(char *) = dlsym(image, "LLVMDisposeMessage");
    LLVMMemoryBufferRef (*writeLLVMBitcode)(LLVMModuleRef) =
        dlsym(image, "LLVMWriteBitcodeToMemoryBuffer");
    void (*setTarget)(LLVMModuleRef, const char *) =
        dlsym(image, "LLVMSetTarget");
    int (*downgrade)(LLVMModuleRef, uint64_t, uint32_t) =
        gpuImage ? dlsym(gpuImage, "MTLDowngradeAIRModule") : 0;
    LLVMMemoryBufferRef (*writeBitcode)(LLVMModuleRef, unsigned) =
        gpuImage ? dlsym(gpuImage, "MTLWriteAIRBitcodeToMemoryBuffer") : 0;
    const char *(*getBufferStart)(LLVMMemoryBufferRef) =
        dlsym(image, "LLVMGetBufferStart");
    size_t (*getBufferSize)(LLVMMemoryBufferRef) =
        dlsym(image, "LLVMGetBufferSize");
    if (!createBuffer || !parseBitcode || !printModule || !disposeMessage)
        return 2;

    size_t bitcodeLength = (size_t)length - offset;
    if (offset >= 8) {
        uint32_t encodedLength = 0;
        memcpy(&encodedLength, bytes + offset - 8, sizeof(encodedLength));
        if (encodedLength && encodedLength <= bitcodeLength)
            bitcodeLength = encodedLength;
    }
    fprintf(stderr, "bitcode offset=0x%zx length=0x%zx\n", offset,
            bitcodeLength);
    LLVMMemoryBufferRef buffer = createBuffer(
        (const char *)bytes + offset, bitcodeLength, "PVG AIR");
    LLVMModuleRef module = 0;
    if (parseBitcode(buffer, &module)) return 3;
    if (getenv("PVG_CANONICALIZE_AIR_BUFFERS")) {
        void *(*getContext)(void *)=dlsym(image,"LLVMGetModuleContext");
        int changed=getContext ? PVGCanonicalizeAIRBuffers(image,module,getContext(module)) : -1;
        fprintf(stderr,"canonicalized AIR buffers=%d\n",changed);
        if(changed<0) return 10;
    }

    const char *isolateStage = getenv("PVG_ISOLATE_STAGE");
    if (isolateStage && *isolateStage) {
        unsigned (*getNamedMetadataNumOperands)(LLVMModuleRef, const char *) =
            dlsym(image, "LLVMGetNamedMetadataNumOperands");
        void (*getNamedMetadataOperands)(LLVMModuleRef, const char *, void **) =
            dlsym(image, "LLVMGetNamedMetadataOperands");
        void *(*getOperand)(void *, unsigned) =
            dlsym(image, "LLVMGetOperand");
        const char *(*getValueName)(void *) =
            dlsym(image, "LLVMGetValueName");
        void (*deleteFunction)(void *) =
            dlsym(image, "LLVMDeleteFunction");
        void *(*getNamedMetadata)(LLVMModuleRef, const void *) = dlsym(
            image, "_ZNK4llvm6Module16getNamedMetadataERKNS_5TwineE");
        void (*eraseNamedMetadata)(LLVMModuleRef, void *) = dlsym(
            image, "_ZN4llvm6Module18eraseNamedMetadataEPNS_11NamedMDNodeE");
        typedef struct {
            const char *lhs;
            const void *rhs;
            uint8_t lhsKind;
            uint8_t rhsKind;
            uint8_t padding[6];
        } PVGTwine;
        const char *names[] = {"air.vertex", "air.fragment", "air.kernel"};
        for (unsigned nameIndex = 0; nameIndex < 3; ++nameIndex) {
            if (strcmp(names[nameIndex] + 4, isolateStage) == 0)
                continue;
            unsigned count = getNamedMetadataNumOperands(
                module, names[nameIndex]);
            void **metadata = count ? calloc(count, sizeof(*metadata)) : NULL;
            void **functions = count ? calloc(count, sizeof(*functions)) : NULL;
            if (count && (!metadata || !functions))
                return 70;
            if (count) {
                getNamedMetadataOperands(module, names[nameIndex], metadata);
                for (unsigned operand = 0; operand < count; ++operand) {
                    functions[operand] = getOperand(metadata[operand], 0);
                    fprintf(stderr, "isolate %s operand=%u function=%s\n",
                            names[nameIndex], operand,
                            getValueName(functions[operand]));
                }
            }
            PVGTwine name = {names[nameIndex], NULL, 3, 1, {0}};
            void *node = getNamedMetadata(module, &name);
            if (node)
                eraseNamedMetadata(module, node);
            for (unsigned operand = 0; operand < count; ++operand)
                deleteFunction(functions[operand]);
            free(functions);
            free(metadata);
        }
    }

    /* Exercise the intended old-LLVM/new-LLVM membrane without allowing a
     * C++ object from one implementation to enter the other. */
    const char *targetImagePath = getenv("PVG_LLVM_TARGET_IMAGE");
    if (targetImagePath) {
        if (!writeLLVMBitcode || !getBufferStart || !getBufferSize)
            return 10;
        LLVMMemoryBufferRef serialized = writeLLVMBitcode(module);
        if (!serialized)
            return 11;
        void *targetImage = dlopen(targetImagePath, RTLD_NOW | RTLD_LOCAL);
        if (!targetImage) {
            fprintf(stderr, "target dlopen: %s\n", dlerror());
            return 12;
        }
        LLVMMemoryBufferRef (*targetCreateBuffer)(const char *, size_t,
                                                   const char *) =
            dlsym(targetImage, "LLVMCreateMemoryBufferWithMemoryRangeCopy");
        int (*targetParseBitcode)(LLVMMemoryBufferRef, LLVMModuleRef *) =
            dlsym(targetImage, "LLVMParseBitcode2");
        char *(*targetPrintModule)(LLVMModuleRef) =
            dlsym(targetImage, "LLVMPrintModuleToString");
        void (*targetDisposeMessage)(char *) =
            dlsym(targetImage, "LLVMDisposeMessage");
        if (!targetCreateBuffer || !targetParseBitcode ||
            !targetPrintModule || !targetDisposeMessage)
            return 13;
        LLVMMemoryBufferRef targetBuffer = targetCreateBuffer(
            getBufferStart(serialized), getBufferSize(serialized),
            "PVG bridged AIR");
        LLVMModuleRef targetModule = 0;
        if (!targetBuffer || targetParseBitcode(targetBuffer, &targetModule))
            return 14;
        char *targetText = targetPrintModule(targetModule);
        if (!targetText)
            return 15;
        fprintf(stderr, "bitcode membrane bytes=0x%zx\n",
                getBufferSize(serialized));
        fputs(targetText, stdout);
        targetDisposeMessage(targetText);
        return 0;
    }
    if (argc >= 3 && !pluginMode) {
        if (!setTarget || !downgrade || !writeBitcode || !getBufferStart ||
            !getBufferSize)
            return 5;
        const char *target=getenv("PVG_AIR_OS_TARGET");
        if(target && strcmp(target,"10.14") && strcmp(target,"10.15"))return 64;
        int mojave=target && !strcmp(target,"10.14");
        if (!downgrade(module, mojave?UINT64_C(0x100000002):UINT64_C(0x200000002), 0))
            return 6;
        setTarget(module, mojave?"air64-apple-macosx10.14.0":"air64-apple-macosx10.15.0");
        LLVMMemoryBufferRef rewritten = writeBitcode(module, 0);
        if (!rewritten) return 7;
        FILE *output = fopen(argv[2], "wb");
        if (!output) return 73;
        size_t rewrittenLength = getBufferSize(rewritten);
        if (fwrite(getBufferStart(rewritten), 1, rewrittenLength, output) !=
            rewrittenLength)
            return 74;
        fclose(output);
        fprintf(stderr, "rewritten length=0x%zx\n", rewrittenLength);
    }
    if (pluginMode) {
        void *plugin = dlopen(pluginOverride, RTLD_NOW | RTLD_LOCAL);
        void *(*createCompiler)(void) = plugin ?
            dlsym(plugin, "MTLCompilerCreate") : 0;
        int (*buildRequest)(void *, const void *, size_t, unsigned,
                            LLVMModuleRef, const void **, size_t *, void **,
                            size_t *, void **, size_t *, const char **) =
            plugin ? dlsym(plugin, "MTLCompilerBuildRequestWithOptions") : 0;
        void (*releaseReply)(void *) = plugin ?
            dlsym(plugin, "MTLCompilerReleaseReply") : 0;
        void (*deleteCompiler)(void *) = plugin ?
            dlsym(plugin, "MTLCompilerDelete") : 0;
        if (!plugin || !createCompiler || !buildRequest || !releaseReply ||
            !deleteCompiler) {
            fprintf(stderr, "plugin load: %s\n", dlerror());
            return 8;
        }
        void *compiler = createCompiler();
        const void *data = 0;
        size_t dataLength = 0;
        void *unused1 = 0, *unused2 = 0;
        size_t unusedLength1 = 0, unusedLength2 = 0;
        const char *error = 0;
        int ok = buildRequest(compiler, 0, 0, 0, module, &data, &dataLength,
                              &unused1, &unusedLength1, &unused2,
                              &unusedLength2, &error);
        fprintf(stderr, "plugin result=%d bytes=%zu error=%s\n", ok,
                dataLength, error ? error : "none");
        if (ok == 0 && data && dataLength) {
            FILE *metallib = fopen(argv[2], "wb");
            if (!metallib) return 73;
            fwrite(data, 1, dataLength, metallib);
            fclose(metallib);
        }
        releaseReply(compiler);
        deleteCompiler(compiler);
        if (ok != 0) return 9;
        // A successful real-plugin request consumes the LLVM module through
        // LLVMExtraMakeSharedModule. Do not print or dispose it afterward.
        return 0;
    }
    char *text = printModule(module);
    if (!text) return 4;
    fputs(text, stdout);
    disposeMessage(text);
    return 0;
}
