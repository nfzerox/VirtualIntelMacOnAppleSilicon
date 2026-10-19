#include <dlfcn.h>
#include <pthread.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef PVG_GL_FIX_FETCH_METADATA
#include "../PVGGLAIRMetadata.h"
#endif

/* Serialize with Catalina LLVM, then parse with Big Sur LLVM. */
typedef void *LLVMModuleRef;
typedef void *LLVMMemoryBufferRef;

extern LLVMMemoryBufferRef LLVMWriteBitcodeToMemoryBuffer(LLVMModuleRef);
extern const char *LLVMGetBufferStart(LLVMMemoryBufferRef);
extern size_t LLVMGetBufferSize(LLVMMemoryBufferRef);
extern void LLVMDisposeMemoryBuffer(LLVMMemoryBufferRef);

typedef void *(*CreateCompilerFn)(void);
typedef int (*BuildRequestFn)(void *, const void *, size_t, unsigned,
                              LLVMModuleRef, const void **, size_t *,
                              const void **, size_t *, const void **, size_t *,
                              const char **);
typedef void (*ReleaseReplyFn)(void *);
typedef void (*DeleteCompilerFn)(void *);
typedef LLVMMemoryBufferRef (*CreateBufferFn)(const char *, size_t,
                                               const char *);
typedef int (*ParseBitcodeFn)(LLVMMemoryBufferRef, LLVMModuleRef *);
typedef void (*DisposeModuleFn)(LLVMModuleRef);
typedef void (*DisposeBufferFn)(LLVMMemoryBufferRef);

typedef struct {
    void *realCompiler;
    LLVMMemoryBufferRef oldSerialized;
    LLVMMemoryBufferRef newBacking;
    LLVMModuleRef newModule;
    pthread_mutex_t lock;
#ifdef PVG_GL_DIAGNOSTIC_IR
    char error[65536];
#else
    char error[192];
#endif
} PVGCompilerBridge;

static pthread_once_t gOnce = PTHREAD_ONCE_INIT;
static void *gPluginImage;
static void *gLLVM7Image;
static CreateCompilerFn gCreate;
static BuildRequestFn gBuild;
static ReleaseReplyFn gRelease;
static DeleteCompilerFn gDelete;
static CreateBufferFn gCreateBuffer;
static ParseBitcodeFn gParseBitcode;
static DisposeModuleFn gDisposeModule;
static DisposeBufferFn gDisposeBuffer;
static const char *gLoadError;

static const char kLLVM7Path[] =
    "/System/Library/PrivateFrameworks/GPUCompiler.framework/Versions/"
    "31001/Libraries/libLLVM.dylib";

static void setLoadError(const char *message) {
    if (!gLoadError)
        gLoadError = message;
}

static void loadImplementations(void) {
    const char *llvm7Path = getenv("PVG_LLVM7_IMAGE");
    if (!llvm7Path || !*llvm7Path)
        llvm7Path = kLLVM7Path;
    gLLVM7Image = dlopen(llvm7Path, RTLD_NOW | RTLD_LOCAL);
    if (!gLLVM7Image) {
        setLoadError(dlerror());
        return;
    }
    gCreateBuffer = (CreateBufferFn)dlsym(
        gLLVM7Image, "LLVMCreateMemoryBufferWithMemoryRangeCopy");
    gParseBitcode = (ParseBitcodeFn)dlsym(gLLVM7Image, "LLVMParseBitcode2");
    gDisposeModule = (DisposeModuleFn)dlsym(gLLVM7Image, "LLVMDisposeModule");
    gDisposeBuffer =
        (DisposeBufferFn)dlsym(gLLVM7Image, "LLVMDisposeMemoryBuffer");
    if (!gCreateBuffer || !gParseBitcode || !gDisposeModule ||
        !gDisposeBuffer) {
        setLoadError("Big Sur libLLVM lacks a required bitcode C API");
        return;
    }

    Dl_info ownImage;
    char realPluginPath[PATH_MAX];
    if (!dladdr((const void *)&loadImplementations, &ownImage) ||
        !ownImage.dli_fname) {
        setLoadError("cannot locate the PVG compiler bridge image");
        return;
    }
    const char *lastSlash = strrchr(ownImage.dli_fname, '/');
    size_t directoryLength = lastSlash ?
        (size_t)(lastSlash - ownImage.dli_fname + 1) : 0;
    const char realName[] = "libAppleParavirtCompilerPluginReal.dylib";
    if (directoryLength + sizeof(realName) > sizeof(realPluginPath)) {
        setLoadError("PVG compiler plugin path is too long");
        return;
    }
    memcpy(realPluginPath, ownImage.dli_fname, directoryLength);
    memcpy(realPluginPath + directoryLength, realName, sizeof(realName));
    gPluginImage = dlopen(realPluginPath, RTLD_NOW | RTLD_LOCAL);
    if (!gPluginImage) {
        setLoadError(dlerror());
        return;
    }
    gCreate = (CreateCompilerFn)dlsym(gPluginImage, "MTLCompilerCreate");
    gBuild = (BuildRequestFn)dlsym(
        gPluginImage, "MTLCompilerBuildRequestWithOptions");
    gRelease =
        (ReleaseReplyFn)dlsym(gPluginImage, "MTLCompilerReleaseReply");
    gDelete = (DeleteCompilerFn)dlsym(gPluginImage, "MTLCompilerDelete");
    if (!gCreate || !gBuild || !gRelease || !gDelete)
        setLoadError("real PVG compiler plugin lacks a required entry point");
}

static void disposeBridgedModule(PVGCompilerBridge *bridge) {
    if (bridge->newModule) {
        gDisposeModule(bridge->newModule);
        bridge->newModule = NULL;
    }
    if (bridge->newBacking) {
        gDisposeBuffer(bridge->newBacking);
        bridge->newBacking = NULL;
    }
    if (bridge->oldSerialized) {
        LLVMDisposeMemoryBuffer(bridge->oldSerialized);
        bridge->oldSerialized = NULL;
    }
}

void *MTLCompilerCreate(void) {
    pthread_once(&gOnce, loadImplementations);
    if (gLoadError || !gCreate) {
        fprintf(stderr, "PVG compiler bridge: %s\n", gLoadError ?: "missing compiler entry");
        return NULL;
    }
    PVGCompilerBridge *bridge = calloc(1, sizeof(*bridge));
    if (!bridge)
        return NULL;
    if (pthread_mutex_init(&bridge->lock, NULL) != 0) {
        free(bridge);
        return NULL;
    }
    bridge->realCompiler = gCreate();
    if (!bridge->realCompiler) {
        pthread_mutex_destroy(&bridge->lock);
        free(bridge);
        return NULL;
    }
    return bridge;
}

int MTLCompilerBuildRequestWithOptions(
    void *opaque, const void *options, size_t optionsLength,
    unsigned requestType, LLVMModuleRef oldModule, const void **libraryData,
    size_t *libraryLength, const void **reflectionData,
    size_t *reflectionLength, const void **diagnosticsData,
    size_t *diagnosticsLength, const char **error) {
    PVGCompilerBridge *bridge = opaque;
    if (!bridge || !oldModule) {
        if (error)
            *error = gLoadError ? gLoadError :
                (!bridge ? "PVG compiler bridge creation failed" :
                 "PVG compiler bridge received no LLVM module");
        return 1;
    }

    pthread_mutex_lock(&bridge->lock);
    if (bridge->newModule || bridge->newBacking || bridge->oldSerialized) {
        gRelease(bridge->realCompiler);
        disposeBridgedModule(bridge);
    }

    bridge->oldSerialized = LLVMWriteBitcodeToMemoryBuffer(oldModule);
    if (!bridge->oldSerialized) {
        snprintf(bridge->error, sizeof(bridge->error),
                 "Catalina LLVM failed to serialize the AIR module");
        if (error)
            *error = bridge->error;
        pthread_mutex_unlock(&bridge->lock);
        return 1;
    }

    bridge->newBacking = gCreateBuffer(
        LLVMGetBufferStart(bridge->oldSerialized),
        LLVMGetBufferSize(bridge->oldSerialized), "PVG LLVM 5-to-7 bridge");
    if (!bridge->newBacking ||
        gParseBitcode(bridge->newBacking, &bridge->newModule) != 0 ||
        !bridge->newModule) {
        snprintf(bridge->error, sizeof(bridge->error),
                 "Big Sur LLVM failed to parse Catalina AIR bitcode");
        if (error)
            *error = bridge->error;
        disposeBridgedModule(bridge);
        pthread_mutex_unlock(&bridge->lock);
        return 1;
    }

#ifdef PVG_GL_FIX_FETCH_METADATA
    if (PVGCompleteGLFetchMetadata(gLLVM7Image, bridge->newModule) < 0) {
        if(error) *error="PVG GL vertex-fetch metadata could not be completed";
        disposeBridgedModule(bridge);
        pthread_mutex_unlock(&bridge->lock);
        return 1;
    }
    /* The donor GL frontend can emit AIR 2.1 texture intrinsics. The PVG
     * backend downgrades newer AIR, but does not upgrade older signatures.
     * Normalize GL modules with Apple's own upgrader before that downgrade. */
    void *(*namedFunction)(void *, const char *) = dlsym(gLLVM7Image, "LLVMGetNamedFunction");
    if (namedFunction && namedFunction(bridge->newModule, "#internal_main")) {
        void *gpu = dlopen("/System/Library/PrivateFrameworks/GPUCompiler.framework/Versions/31001/Libraries/libGPUCompiler.dylib", RTLD_NOW | RTLD_LOCAL);
        bool (*upgrade)(void *, uint64_t, uint32_t) = gpu ? dlsym(gpu, "MTLUpgradeAIRModule") : NULL;
        bool upgraded = upgrade && upgrade(bridge->newModule, UINT64_C(0x300000002), 0);
        if (gpu) dlclose(gpu);
        if (!upgraded) {
            if (error) *error = "PVG GL AIR upgrade failed";
            disposeBridgedModule(bridge);
            pthread_mutex_unlock(&bridge->lock);
            return 1;
        }
    }
#endif
#ifdef PVG_GL_DIAGNOSTIC_IR
    /* Development-only: return input through the normal compiler error reply,
     * not a filesystem write from the sandboxed compiler service. */
    char *(*printIR)(void *) = dlsym(gLLVM7Image, "LLVMPrintModuleToString");
    void (*freeIR)(char *) = dlsym(gLLVM7Image, "LLVMDisposeMessage");
    char *ir = printIR ? printIR(bridge->newModule) : NULL;
    snprintf(bridge->error, sizeof(bridge->error), "PVG diagnostic AIR:\n%s", ir ?: "unavailable");
    if (ir && freeIR) freeIR(ir);
    if (error) *error = bridge->error;
    disposeBridgedModule(bridge);
    pthread_mutex_unlock(&bridge->lock);
    return 1;
#endif
    int result = gBuild(
        bridge->realCompiler, options, optionsLength, requestType,
        bridge->newModule, libraryData, libraryLength, reflectionData,
        reflectionLength, diagnosticsData, diagnosticsLength, error);
    /* The real plugin transfers a successful module into
     * LLVMExtraMakeSharedModule and releases that shared owner before it
     * returns. Disposing it here would be a second destruction. A failed
     * downgrade returns before the ownership transfer, so retain the pointer
     * only on failure for our normal cleanup path. */
    if (result == 0)
        bridge->newModule = NULL;
    pthread_mutex_unlock(&bridge->lock);
    return result;
}

void MTLCompilerReleaseReply(void *opaque) {
    PVGCompilerBridge *bridge = opaque;
    if (!bridge)
        return;
    pthread_mutex_lock(&bridge->lock);
    gRelease(bridge->realCompiler);
    disposeBridgedModule(bridge);
    pthread_mutex_unlock(&bridge->lock);
}

void MTLCompilerDelete(void *opaque) {
    PVGCompilerBridge *bridge = opaque;
    if (!bridge)
        return;
    pthread_mutex_lock(&bridge->lock);
    gRelease(bridge->realCompiler);
    disposeBridgedModule(bridge);
    gDelete(bridge->realCompiler);
    bridge->realCompiler = NULL;
    pthread_mutex_unlock(&bridge->lock);
    pthread_mutex_destroy(&bridge->lock);
    free(bridge);
}
