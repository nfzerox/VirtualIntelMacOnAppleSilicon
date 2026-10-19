#include <Headers/kern_api.hpp>
#include <Headers/kern_patcher.hpp>
#include <Headers/kern_util.hpp>
#include <Headers/plugin_start.hpp>

#include <IOKit/IOLib.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/graphics/IOGraphicsTypes.h>
#include <libkern/c++/OSArray.h>
#include <libkern/libkern.h>
#include <mach/mach_time.h>

#include <stdint.h>
#include "PVGMojaveSurface.h"
#include "PVGSurfaceLimits.h"

#ifndef PVG_TRACE_DISPLAY
#define PVG_TRACE_DISPLAY 0
#endif

namespace {

constexpr uint32_t kCreateSurfaceSelector = 0;
constexpr uint32_t kLockSurfaceSelector = 2;
constexpr uint32_t kLookupPortSelector = 34;
constexpr uint32_t kCreatePortSelector = 35;

constexpr uint32_t kCatalinaResultSize = 0xdd0;
constexpr uint32_t kBigSurResultSize = 0xf60;
constexpr size_t kCommonSize = 0x44;
constexpr size_t kCatalinaPlaneSize = 0x6c;
constexpr size_t kBigSurPlaneSize = 0x78;
constexpr size_t kBigSurPlaneOffset = 0x54;
constexpr size_t kPlaneCount = 32;
constexpr size_t kTrailerSize = 0xc;
constexpr uint32_t kParallelsBootWidth = 1024;
constexpr uint32_t kParallelsBootHeight = 768;
constexpr size_t kDisplayTransactionSize = 0x108;
constexpr uint64_t kDisplayTransactionTimestampFlag = 0x100;

using ExternalMethod = IOReturn (*)(
    IOUserClient *, uint32_t, IOExternalMethodArguments *,
    IOExternalMethodDispatch *, OSObject *, void *);

ExternalMethod originalExternalMethod = nullptr;

using PopulateConfig = void (*)(OSObject *, void *);
PopulateConfig originalPopulateConfig = nullptr;
uint32_t surfaceDimensionsOffset = 0;
uint32_t populatedDimensionsOffset = 0;

void wrappedPopulateConfig(OSObject *accelerator, void *config) {
    originalPopulateConfig(accelerator, config);
    // PVG's own populator overwrites the generic initializer's dimensions.
    // Correct the final configuration before start() publishes it to IOSurface.
    // Cross-check independently recovered object/config offsets at runtime.
    if (!surfaceDimensionsOffset || !populatedDimensionsOffset || !accelerator || !config ||
        !accelerator->metaCast("AppleParavirtAccelerator")) return;
    if (reinterpret_cast<uintptr_t>(config)+populatedDimensionsOffset !=
        reinterpret_cast<uintptr_t>(accelerator)+surfaceDimensionsOffset) return;
    auto pair = reinterpret_cast<volatile uint32_t *>(
        reinterpret_cast<uintptr_t>(accelerator)+surfaceDimensionsOffset);
    if (*pair != 0x10001000) return;
    *pair = PVGSurfaceLimits::supportedDimension |
        (PVGSurfaceLimits::supportedDimension << 16);
    SYSLOG("iosabi", "selected 8192x8192 PVG surface dimensions before registration");
}

void discoverSurfaceDimensions(KernelPatcher &patcher, size_t index,
    mach_vm_address_t address, size_t size) {
    if (getKernelVersion() != KernelVersion::Mojave &&
        getKernelVersion() != KernelVersion::Catalina) return;
    const char *symbol = "__ZN22IOGraphicsAccelerator225initializeConfigStructureEv";
    const auto initialize = patcher.solveSymbol(index, symbol);
    const auto validator = patcher.solveSymbol(index,
        "__ZN17IOAccelSurfaceMTL22isSurfaceSizeSupportedEss");
    constexpr size_t initBytes = 256, validationBytes = 64;
    if (!initialize || !validator || initialize < address || validator < address ||
        initialize-address > size || validator-address > size ||
        size-(initialize-address) < initBytes || size-(validator-address) < validationBytes ||
        !PVGSurfaceLimits::findDimensionOffset(reinterpret_cast<const uint8_t *>(initialize), initBytes,
            reinterpret_cast<const uint8_t *>(validator), validationBytes, surfaceDimensionsOffset)) {
        SYSLOG("iosabi", "unknown accelerator surface layout; retained native limits");
        patcher.clearError(); return;
    }
    SYSLOG("iosabi", "recognized accelerator surface dimension layout");
}

void installPopulatedSurfaceDimensions(KernelPatcher &patcher, size_t index,
    mach_vm_address_t address, size_t size) {
    if (getKernelVersion() != KernelVersion::Mojave &&
        getKernelVersion() != KernelVersion::Catalina) return;
    const char *symbol = "__ZN24AppleParavirtAccelerator19populateAccelConfigEP13IOAccelConfig";
    const auto populate = patcher.solveSymbol(index, symbol);
    constexpr size_t bytes = 128;
    if (!populate || populate < address || populate-address > size ||
        size-(populate-address) < bytes ||
        !PVGSurfaceLimits::findPopulatedDimensionOffset(reinterpret_cast<const uint8_t *>(populate),bytes,populatedDimensionsOffset)) {
        SYSLOG("iosabi", "unknown PVG config populator; retained native limits");
        patcher.clearError();return;
    }
    KernelPatcher::RouteRequest request {symbol, wrappedPopulateConfig, originalPopulateConfig};
    if (!patcher.routeMultiple(index, &request, 1)) {
        populatedDimensionsOffset = 0;
        SYSLOG("iosabi", "could not route PVG configuration; retained native limits");
        patcher.clearError(); return;
    }
    SYSLOG("iosabi", "installed final PVG surface dimension configuration");
}

using DisplayPipeTransactionEnd = IOReturn (*)(
    IOUserClient *, void *, IOExternalMethodArguments *);

DisplayPipeTransactionEnd originalDisplayPipeTransactionEnd = nullptr;

using DisplayTimeout = void (*)(void *, void *);
using DisplayInterrupt = void (*)(void *, void *);
DisplayTimeout originalDisplayTimeout = nullptr;
DisplayInterrupt signalDisplayTransactionInterrupt = nullptr;

// Mojave diagnostic recovery: the donor timeout returns without retiring an
// already-complete fence. Re-signal its normal handler, which checks the real
// fence before retiring anything. No fence values or queue state are forged.
void wrappedDisplayTimeout(void *pipe, void *timer) {
    originalDisplayTimeout(pipe, timer);
    signalDisplayTransactionInterrupt(pipe, nullptr);
    SYSLOG("iosabi", "Mojave display timeout: re-signalled completion handler");
}

using GetPlaneIOSurface = void *(*)(void *, uint32_t, uint32_t);
using GetSurfaceID = uint32_t (*)(void *);
using SubmitDisplayTransaction = IOReturn (*)(void *, void *);

GetPlaneIOSurface getPlaneIOSurface = nullptr;
GetSurfaceID getSurfaceID = nullptr;
SubmitDisplayTransaction originalSubmitDisplayTransaction = nullptr;
uint32_t displayTransactionTraceCount = 0;
uint32_t submitTransactionTraceCount = 0;

using SetDetailedTimings = IOReturn (*)(void *, OSArray *);
using SetFramebufferMode = IOReturn (*)(void *, int, int);
using GetPixelInformation = IOReturn (*)(
    void *, IODisplayModeID, IOIndex, IOPixelAperture,
    IOPixelInformation *);

SetDetailedTimings originalSetDetailedTimings = nullptr;
SetFramebufferMode setStartupDisplayMode = nullptr;
SetFramebufferMode setDisplayMode = nullptr;
GetPixelInformation originalGetPixelInformation = nullptr;

const char *iosurfacePaths[] = {
    "/System/Library/Extensions/IOSurface.kext/Contents/MacOS/IOSurface"
};

KernelPatcher::KextInfo iosurfaceKext {
    "com.apple.iokit.IOSurface",
    iosurfacePaths,
    arrsize(iosurfacePaths),
    {true},
    {},
    KernelPatcher::KextInfo::Unloaded
};

const char *ioacceleratorPaths[] = {
    "/System/Library/Extensions/IOAcceleratorFamily2.kext/Contents/MacOS/IOAcceleratorFamily2"
};

KernelPatcher::KextInfo ioacceleratorKext {
    "com.apple.iokit.IOAcceleratorFamily2",
    ioacceleratorPaths,
    arrsize(ioacceleratorPaths),
    {true},
    {},
    KernelPatcher::KextInfo::Unloaded
};

const char *appleParavirtGPUPaths[] = {
    "/System/Library/Extensions/AppleParavirtGPU.kext/Contents/MacOS/AppleParavirtGPU"
};

KernelPatcher::KextInfo appleParavirtGPUKext {
    "com.apple.driver.AppleParavirtGPU",
    appleParavirtGPUPaths,
    arrsize(appleParavirtGPUPaths),
    {true},
    {},
    KernelPatcher::KextInfo::Unloaded
};

void translateResult(void *catalinaOutput, const void *bigSurOutput) {
    auto destination = static_cast<uint8_t *>(catalinaOutput);
    auto source = static_cast<const uint8_t *>(bigSurOutput);

    memcpy(destination, source, kCommonSize);
    for (size_t plane = 0; plane < kPlaneCount; ++plane) {
        memcpy(destination + kCommonSize + plane * kCatalinaPlaneSize,
            source + kBigSurPlaneOffset + plane * kBigSurPlaneSize,
            kCatalinaPlaneSize);
    }
    memcpy(destination + kCommonSize + kPlaneCount * kCatalinaPlaneSize,
        source + kBigSurPlaneOffset + kPlaneCount * kBigSurPlaneSize,
        kTrailerSize);
}

IOReturn wrappedExternalMethod(
    IOUserClient *client,
    uint32_t selector,
    IOExternalMethodArguments *arguments,
    IOExternalMethodDispatch *dispatch,
    OSObject *target,
    void *reference) {
    if (!arguments || !originalExternalMethod) {
        return kIOReturnBadArgument;
    }

    if (getKernelVersion() == KernelVersion::Mojave ||
        getKernelVersion() == KernelVersion::HighSierra) {
        const bool highSierra = getKernelVersion() == KernelVersion::HighSierra;
        uint32_t donorSelector;
        if (highSierra && selector > 28) return kIOReturnUnsupported;
        if (!PVGMojaveSurface::mapSelector(selector, donorSelector))
            return kIOReturnUnsupported;

        const uint32_t savedSelector = arguments->selector;
        arguments->selector = donorSelector;
        void *savedOutput = arguments->structureOutput;
        const uint32_t savedSize = arguments->structureOutputSize;
        const uint64_t *savedInput = arguments->scalarInput;
        const uint32_t savedCount = arguments->scalarInputCount;
        uint64_t portInput[2] {};
        void *translated = nullptr;
        if (PVGMojaveSurface::returnsSurface(selector) && savedOutput &&
            savedSize == (highSierra ? PVGMojaveSurface::highSierraResultSize :
                                      PVGMojaveSurface::resultSize)) {
            translated = IOMalloc(PVGMojaveSurface::donorResultSize);
            if (!translated) {
                arguments->selector = savedSelector;
                return kIOReturnNoMemory;
            }
            bzero(translated, PVGMojaveSurface::donorResultSize);
            arguments->structureOutput = translated;
            arguments->structureOutputSize = PVGMojaveSurface::donorResultSize;
        }
        if (selector == 28 && savedInput && savedCount == 1) {
            portInput[0] = savedInput[0];
            arguments->scalarInput = portInput;
            arguments->scalarInputCount = 2;
        }
        const IOReturn result = originalExternalMethod(client, donorSelector,
            arguments, dispatch, target, reference);
        if (translated) {
            if (result == kIOReturnSuccess && arguments->structureOutputSize >=
                PVGMojaveSurface::donorResultSize)
                {
                    if (highSierra) PVGMojaveSurface::translateHighSierraResult(savedOutput, translated);
                    else PVGMojaveSurface::translateResult(savedOutput, translated);
                }
            arguments->structureOutput = savedOutput;
            arguments->structureOutputSize = savedSize;
            IOFree(translated, PVGMojaveSurface::donorResultSize);
        }
        arguments->scalarInput = savedInput;
        arguments->scalarInputCount = savedCount;
        arguments->selector = savedSelector;
        return result;
    }

    if ((selector == kCreateSurfaceSelector ||
            selector == kLockSurfaceSelector ||
            selector == kLookupPortSelector) &&
        arguments->structureOutput &&
        arguments->structureOutputSize == kCatalinaResultSize) {
        void *catalinaOutput = arguments->structureOutput;
        void *bigSurOutput = IOMalloc(kBigSurResultSize);
        if (!bigSurOutput) {
            return kIOReturnNoMemory;
        }

        bzero(bigSurOutput, kBigSurResultSize);
        arguments->structureOutput = bigSurOutput;
        arguments->structureOutputSize = kBigSurResultSize;

        const IOReturn result = originalExternalMethod(
            client, selector, arguments, dispatch, target, reference);

        if (result == kIOReturnSuccess &&
            arguments->structureOutputSize >= kBigSurResultSize) {
            translateResult(catalinaOutput, bigSurOutput);
        }

        arguments->structureOutput = catalinaOutput;
        arguments->structureOutputSize = kCatalinaResultSize;
        IOFree(bigSurOutput, kBigSurResultSize);
        return result;
    }

    if (selector == kCreatePortSelector &&
        arguments->scalarInput &&
        arguments->scalarInputCount == 1) {
        const uint64_t *catalinaInput = arguments->scalarInput;
        const uint32_t catalinaInputCount = arguments->scalarInputCount;
        const uint64_t bigSurInput[2] {catalinaInput[0], 0};

        arguments->scalarInput = bigSurInput;
        arguments->scalarInputCount = 2;
        const IOReturn result = originalExternalMethod(
            client, selector, arguments, dispatch, target, reference);
        arguments->scalarInput = catalinaInput;
        arguments->scalarInputCount = catalinaInputCount;
        return result;
    }

    return originalExternalMethod(
        client, selector, arguments, dispatch, target, reference);
}

IOReturn wrappedDisplayPipeTransactionEnd(
    IOUserClient *client,
    void *reference,
    IOExternalMethodArguments *arguments) {
    if (!originalDisplayPipeTransactionEnd) {
        return kIOReturnNotReady;
    }

    const void *originalInput = arguments ? arguments->structureInput : nullptr;
    void *translatedInput = nullptr;
    if (arguments && originalInput &&
        arguments->structureInputSize == kDisplayTransactionSize) {
        const auto originalQword =
            static_cast<const uint64_t *>(originalInput);
        if ((originalQword[0] & kDisplayTransactionTimestampFlag) == 0 &&
            originalQword[1] == 0) {
            translatedInput = IOMalloc(kDisplayTransactionSize);
            if (!translatedInput) {
                return kIOReturnNoMemory;
            }
            memcpy(translatedInput, originalInput, kDisplayTransactionSize);
            auto translatedQword = static_cast<uint64_t *>(translatedInput);
            translatedQword[0] |= kDisplayTransactionTimestampFlag;
            translatedQword[1] = mach_absolute_time();
            arguments->structureInput = translatedInput;
        }
    }

#if PVG_TRACE_DISPLAY
    const uint32_t traceCount = __sync_add_and_fetch(
        &displayTransactionTraceCount, 1);
    const bool shouldTrace = traceCount <= 16 || (traceCount % 512) == 0;
    const auto bytes = arguments && arguments->structureInput
        ? static_cast<const uint8_t *>(arguments->structureInput)
        : nullptr;
    const auto qword = reinterpret_cast<const uint64_t *>(bytes);
    if (shouldTrace && bytes && arguments->structureInputSize >= 0x60) {
        SYSLOG("iosabi",
            "display transaction n=%u size=0x%llx q0=%llx q1=%llx q2=%llx q3=%llx q4=%llx q5=%llx",
            traceCount,
            static_cast<unsigned long long>(arguments->structureInputSize),
            qword[0], qword[1], qword[2], qword[3], qword[4], qword[5]);
        SYSLOG("iosabi",
            "display transaction q6=%llx q7=%llx q8=%llx q9=%llx q10=%llx q11=%llx",
            qword[6], qword[7], qword[8], qword[9], qword[10], qword[11]);
    } else if (shouldTrace) {
        SYSLOG("iosabi", "display transaction missing/short input size=0x%llx",
            static_cast<unsigned long long>(
                arguments ? arguments->structureInputSize : 0));
    }

#endif
    const IOReturn result =
        originalDisplayPipeTransactionEnd(client, reference, arguments);
    if (translatedInput) {
        arguments->structureInput = originalInput;
        IOFree(translatedInput, kDisplayTransactionSize);
    }
#if PVG_TRACE_DISPLAY
    if (shouldTrace) {
        SYSLOG("iosabi", "display transaction n=%u result=0x%x",
            traceCount, result);
    }
#endif
    return result;
}

IOReturn wrappedSubmitDisplayTransaction(void *displayPipe, void *transaction) {
    if (!originalSubmitDisplayTransaction) {
        return kIOReturnNotReady;
    }

    // Big Sur's validateTransaction rejects a missing plane-0 IOSurface, but
    // its submitTransaction assumes that validation already happened and
    // panics (AppleParavirtDisplayPipe.cpp:428) before acquiring any resources.
    // Mojave's WindowServer/update teardown can submit that empty transaction.
    // Return the same error as the donor validator; do not forge a successful
    // submission, signal a fence, or reuse a potentially retired surface.
    if (!getPlaneIOSurface) return kIOReturnNotReady;
    void *surface = transaction ? getPlaneIOSurface(transaction, 0, 0) : nullptr;
    if (!surface) {
        static uint32_t rejected = 0;
        if (__sync_add_and_fetch(&rejected, 1) <= 4)
            SYSLOG("iosabi", "rejected display transaction without a plane-0 IOSurface");
        return kIOReturnBadArgument;
    }

#if PVG_TRACE_DISPLAY
    const uint32_t traceCount = __sync_add_and_fetch(
        &submitTransactionTraceCount, 1);
    const bool shouldTrace = traceCount <= 32 || (traceCount % 512) == 0;
    uint32_t surfaceID = 0;
    void *task = nullptr;
    uint32_t resourceHeapID = 0;
    uint32_t stampIndex = 0;
    uint32_t gammaState = 0;

    if (surface && getSurfaceID) {
        surfaceID = getSurfaceID(surface);
    }
    if (displayPipe) {
        const auto bytes = static_cast<const uint8_t *>(displayPipe);
        const auto owner = *reinterpret_cast<void *const *>(bytes + 0x328);
        if (owner) {
            task = *reinterpret_cast<void *const *>(
                static_cast<const uint8_t *>(owner) + 0x150);
        }
        if (task) {
            resourceHeapID = *reinterpret_cast<const uint32_t *>(
                static_cast<const uint8_t *>(task) + 0x268);
        }
        stampIndex = *reinterpret_cast<const uint32_t *>(bytes + 0x380);
        gammaState = *reinterpret_cast<const uint32_t *>(bytes + 0x384);
    }

    if (shouldTrace) {
        SYSLOG("iosabi",
            "APV submit n=%u pipe=%p transaction=%p surface=%p surfaceID=%u task=%p heapID=%u stamp=%u gamma=%u",
            traceCount, displayPipe, transaction, surface, surfaceID, task,
            resourceHeapID, stampIndex, gammaState);
    }
#endif
    const IOReturn result = originalSubmitDisplayTransaction(
        displayPipe, transaction);
#if PVG_TRACE_DISPLAY
    if (shouldTrace) {
        SYSLOG("iosabi", "APV submit n=%u result=0x%x", traceCount, result);
    }
#endif
    return result;
}

IOReturn wrappedSetDetailedTimings(void *framebuffer, OSArray *timings) {
    if (!originalSetDetailedTimings) {
        return kIOReturnNotReady;
    }

    const IOReturn result = originalSetDetailedTimings(framebuffer, timings);
    if (result != kIOReturnSuccess || !timings || timings->getCount() == 0) {
        SYSLOG("iosabi",
            "framebuffer detailed timings result=0x%x count=%u; mode unchanged",
            result, timings ? timings->getCount() : 0);
        return result;
    }

    // AppleParavirtFramebuffer::start initializes its startup and current mode
    // IDs to -1. Catalina's display-pipe setup runs immediately after detailed
    // timings are installed and rejects that invalid mode before WindowServer
    // gets a chance to select one. The donor exposes timing array indices as
    // display mode IDs, so select mode 0 synchronously at the publication point.
    const IOReturn startupResult = setStartupDisplayMode
        ? setStartupDisplayMode(framebuffer, 0, 2)
        : kIOReturnNotReady;
    const IOReturn modeResult = setDisplayMode
        ? setDisplayMode(framebuffer, 0, 2)
        : kIOReturnNotReady;
    SYSLOG("iosabi",
        "framebuffer mode primed after detailed timings count=%u startup=0x%x current=0x%x",
        timings->getCount(), startupResult, modeResult);
    return result;
}

IOReturn wrappedGetPixelInformation(
    void *framebuffer,
    IODisplayModeID displayMode,
    IOIndex depth,
    IOPixelAperture aperture,
    IOPixelInformation *pixelInfo) {
    if (!originalGetPixelInformation) {
        return kIOReturnNotReady;
    }

    const IOReturn result = originalGetPixelInformation(
        framebuffer, displayMode, depth, aperture, pixelInfo);
    if (result == kIOReturnSuccess || !framebuffer || !pixelInfo ||
        aperture != 0 || depth < 0 || depth > 2) {
        return result;
    }

    // Big Sur never asks for pixel information before the detailed-timing
    // array exists, whereas Catalina does while constructing its first
    // IOAccelDisplayPipe. Parallels' pre-WindowServer boot framebuffer is
    // 1024x768x32 (also exposed as the 3 MiB IOFBMemorySize). Seed only this
    // early resource with that format. The first normal detailed-timing mode
    // change replaces it with the actual dynamic guest resolution.
    constexpr uint32_t width = kParallelsBootWidth;
    constexpr uint32_t height = kParallelsBootHeight;

    bzero(pixelInfo, sizeof(*pixelInfo));
    pixelInfo->bytesPerRow = static_cast<uint32_t>(width) * 4;
    pixelInfo->bitsPerPixel = 32;
    pixelInfo->pixelType = kIORGBDirectPixels;
    pixelInfo->componentCount = 3;
    pixelInfo->bitsPerComponent = 8;
    pixelInfo->componentMasks[0] = 0x000000ff;
    pixelInfo->componentMasks[1] = 0x0000ff00;
    pixelInfo->componentMasks[2] = 0x00ff0000;
    strlcpy(pixelInfo->pixelFormat, IO32BitDirectPixels,
        sizeof(pixelInfo->pixelFormat));
    pixelInfo->activeWidth = width;
    pixelInfo->activeHeight = height;
    SYSLOG("iosabi",
        "supplied early framebuffer pixel info mode=%d depth=%d dimensions=%ux%u",
        displayMode, depth, width, height);
    return kIOReturnSuccess;
}

void processIOSurface(
    void *, KernelPatcher &patcher, size_t index,
    mach_vm_address_t, size_t) {
    if (index != iosurfaceKext.loadIndex) {
        return;
    }

#if PVG_TRACE_DISPLAY
    getSurfaceID = reinterpret_cast<GetSurfaceID>(patcher.solveSymbol(
        index, "__ZNK9IOSurface12getSurfaceIDEv"));
    if (!getSurfaceID) {
        SYSLOG("iosabi", "failed to resolve IOSurface::getSurfaceID");
        patcher.clearError();
    }
#endif

    KernelPatcher::RouteRequest request {
        "__ZN23IOSurfaceRootUserClient14externalMethodEjP25IOExternalMethodArgumentsP24IOExternalMethodDispatchP8OSObjectPv",
        wrappedExternalMethod,
        originalExternalMethod
    };

    if (!patcher.routeMultiple(index, &request, 1)) {
        SYSLOG("iosabi", "failed to route IOSurfaceRootUserClient::externalMethod");
        patcher.clearError();
        return;
    }

    SYSLOG("iosabi", "installed native-to-Big-Sur IOSurface ABI bridge (Darwin %d)", getKernelVersion());
}

void processIOAccelerator(
    void *, KernelPatcher &patcher, size_t index,
    mach_vm_address_t address, size_t size) {
    if (index != ioacceleratorKext.loadIndex) {
        return;
    }

    discoverSurfaceDimensions(patcher, index, address, size);

    getPlaneIOSurface = reinterpret_cast<GetPlaneIOSurface>(
        patcher.solveSymbol(index,
            "__ZNK30IOAccelDisplayPipeTransaction217getPlaneIOSurfaceEjj"));
    if (!getPlaneIOSurface) {
        SYSLOG("iosabi", "failed to resolve transaction IOSurface accessor");
        patcher.clearError();
    }

    KernelPatcher::RouteRequest request {
        "__ZN29IOAccelDisplayPipeUserClient217s_transaction_endEPS_PvP25IOExternalMethodArguments",
        wrappedDisplayPipeTransactionEnd,
        originalDisplayPipeTransactionEnd
    };

    if (!patcher.routeMultiple(index, &request, 1)) {
        SYSLOG("iosabi", "failed to route display-pipe transaction end");
        patcher.clearError();
        return;
    }

    SYSLOG("iosabi", "installed display-pipe timestamp compatibility");

    if (getKernelVersion() == KernelVersion::Mojave) {
        signalDisplayTransactionInterrupt = reinterpret_cast<DisplayInterrupt>(
            patcher.solveSymbol(index,
                "__ZN18IOAccelDisplayPipe26signalTransactionInterruptEPv"));
        if (!signalDisplayTransactionInterrupt) {
            SYSLOG("iosabi", "missing display interrupt recovery target");
            patcher.clearError();
            return;
        }
        KernelPatcher::RouteRequest recovery {
            "__ZN18IOAccelDisplayPipe31transaction_timeout_timer_firedEP18IOTimerEventSource",
            wrappedDisplayTimeout, originalDisplayTimeout
        };
        if (!patcher.routeMultiple(index, &recovery, 1)) {
            SYSLOG("iosabi", "failed to route Mojave display timeout recovery");
            patcher.clearError();
            return;
        }
        SYSLOG("iosabi", "installed Mojave display timeout notification recovery");
    }
}

void processAppleParavirtGPU(
    void *, KernelPatcher &patcher, size_t index,
    mach_vm_address_t address, size_t size) {
    if (index != appleParavirtGPUKext.loadIndex) {
        return;
    }

    installPopulatedSurfaceDimensions(patcher, index, address, size);

    setStartupDisplayMode = reinterpret_cast<SetFramebufferMode>(
        patcher.solveSymbol(index,
            "__ZN24AppleParavirtFramebuffer21setStartupDisplayModeEii"));
    setDisplayMode = reinterpret_cast<SetFramebufferMode>(
        patcher.solveSymbol(index,
            "__ZN24AppleParavirtFramebuffer14setDisplayModeEii"));
    if (!setStartupDisplayMode || !setDisplayMode) {
        SYSLOG("iosabi", "failed to resolve donor framebuffer mode methods");
        patcher.clearError();
        return;
    }

    KernelPatcher::RouteRequest requests[] {
        {
            "__ZN24AppleParavirtFramebuffer18setDetailedTimingsEP7OSArray",
            wrappedSetDetailedTimings,
            originalSetDetailedTimings
        },
        {
            "__ZN24AppleParavirtFramebuffer19getPixelInformationEiiiP18IOPixelInformation",
            wrappedGetPixelInformation,
            originalGetPixelInformation
        }
    };
    if (!patcher.routeMultiple(index, requests, arrsize(requests))) {
        SYSLOG("iosabi", "failed to route donor framebuffer compatibility methods");
        patcher.clearError();
        return;
    }

    SYSLOG("iosabi", "installed donor framebuffer initialization compatibility");

    // Do not disturb framebuffer initialization on an unrecognized accessor
    // layout. The guard uses the donor's own method, never raw object offsets.
    if (getPlaneIOSurface) {
        KernelPatcher::RouteRequest submission {
            "__ZN24AppleParavirtDisplayPipe17submitTransactionEP30IOAccelDisplayPipeTransaction2",
            wrappedSubmitDisplayTransaction, originalSubmitDisplayTransaction
        };
        if (!patcher.routeMultiple(index, &submission, 1)) {
            SYSLOG("iosabi", "could not install display submission surface guard");
            patcher.clearError();
        } else {
            SYSLOG("iosabi", "installed display submission surface guard");
        }
    }
}

void startPlugin() {
    const auto iosurfaceError = lilu.onKextLoad(
        &iosurfaceKext, 1, processIOSurface, nullptr);
    if (iosurfaceError != LiluAPI::Error::NoError) {
        SYSLOG("iosabi", "failed to register IOSurface load callback: %d",
            iosurfaceError);
    }

    const auto ioacceleratorError = lilu.onKextLoad(
        &ioacceleratorKext, 1, processIOAccelerator, nullptr);
    if (ioacceleratorError != LiluAPI::Error::NoError) {
        SYSLOG("iosabi", "failed to register IOAccelerator load callback: %d",
            ioacceleratorError);
    }

    const auto appleParavirtGPUError = lilu.onKextLoad(
        &appleParavirtGPUKext, 1, processAppleParavirtGPU, nullptr);
    if (appleParavirtGPUError != LiluAPI::Error::NoError) {
        SYSLOG("iosabi", "failed to register AppleParavirtGPU load callback: %d",
            appleParavirtGPUError);
    }
}

const char *disableArguments[] {"-pvgiosabioff"};
const char *debugArguments[] {"-pvgiosabidbg"};

} // namespace

PluginConfiguration ADDPR(config) {
    xStringify(PRODUCT_NAME),
    parseModuleVersion(xStringify(MODULE_VERSION)),
    LiluAPI::AllowNormal,
    disableArguments,
    arrsize(disableArguments),
    debugArguments,
    arrsize(debugArguments),
    nullptr,
    0,
    KernelVersion::HighSierra,
    KernelVersion::Catalina,
    startPlugin
};
