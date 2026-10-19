#include <IOKit/IOService.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/graphics/IOFramebuffer.h>
#include <libkern/c++/OSNumber.h>

// AppleParavirtGPU's exported framebuffer method.  The prototype is recovered
// from the Big Sur 20G1426 x86_64 symbol and implementation.  The provider
// object is created by AppleParavirtGPU, not by this compatibility service.
class AppleParavirtFramebuffer : public IOFramebuffer {
public:
    IOReturn connectionChange(
        uint32_t connection,
        uint16_t vendor,
        uint16_t product,
        const char *model,
        uint32_t modeCount,
        void (^modeAtIndex)(uint32_t index, uint16_t *width,
                            uint16_t *height, uint32_t *refresh));
};

class PVGFramebufferBootstrap : public IOService {
    OSDeclareDefaultStructors(PVGFramebufferBootstrap)

public:
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;

private:
    static void modeRepairTimerAction(OSObject *owner,
                                      IOTimerEventSource *sender);
    void repairMode(IOTimerEventSource *sender);

    IOTimerEventSource *modeRepairTimer_ = nullptr;
    IOWorkLoop *modeRepairWorkLoop_ = nullptr;
    uint32_t modeRepairAttempts_ = 0;
};

OSDefineMetaClassAndStructors(PVGFramebufferBootstrap, IOService)

namespace {

struct Mode {
    uint16_t width;
    uint16_t height;
};

// Catalina's WindowServer asks Metal for a display-sized composite texture.
// Advertising the donor's maximum 3360x2100 mode makes that allocation fail
// and WindowServer abort. Start with the host console's 1024x768 mode; dynamic
// host-size negotiation can be restored separately after first presentation.
constexpr Mode kFallbackModes[] = {
    {1024, 768},
};

constexpr uint32_t kRefresh60Hz16_16 = 60U << 16;

} // namespace

bool PVGFramebufferBootstrap::start(IOService *provider) {
    if (!IOService::start(provider)) {
        return false;
    }

    // Re-announcing an existing fallback table is intentional. The first
    // announcement can happen before IOGraphics registers its connection
    // interrupt callback; a later invocation then supplies the missed edge.
    const bool reannouncing =
        provider->getProperty("IOFBMemorySize") != nullptr ||
        provider->getProperty("IOFBDetailedTimings") != nullptr;

    auto *framebuffer = static_cast<AppleParavirtFramebuffer *>(provider);
    IOReturn result = framebuffer->connectionChange(
        0, 0x1006, 0, "Apple Virtual Display",
        static_cast<uint32_t>(sizeof(kFallbackModes) / sizeof(kFallbackModes[0])),
        ^(uint32_t index, uint16_t *width, uint16_t *height,
          uint32_t *refresh) {
            if (index >= sizeof(kFallbackModes) / sizeof(kFallbackModes[0])) {
                return;
            }
            if (width != nullptr) {
                *width = kFallbackModes[index].width;
            }
            if (height != nullptr) {
                *height = kFallbackModes[index].height;
            }
            if (refresh != nullptr) {
                *refresh = kRefresh60Hz16_16;
            }
        });

    // IOGraphics installs IOFBDetailedTimings only after this start method
    // returns. Schedule the mode validation on the framebuffer's work loop so
    // it runs as soon as that timing array becomes visible.
    modeRepairWorkLoop_ = framebuffer->getWorkLoop();
    modeRepairTimer_ = IOTimerEventSource::timerEventSource(
        this, &PVGFramebufferBootstrap::modeRepairTimerAction);
    if (modeRepairWorkLoop_ == nullptr || modeRepairTimer_ == nullptr ||
        modeRepairWorkLoop_->addEventSource(modeRepairTimer_) !=
            kIOReturnSuccess) {
        setProperty("PVGFramebufferModeRepairScheduled", false);
        if (modeRepairTimer_ != nullptr) {
            modeRepairTimer_->release();
            modeRepairTimer_ = nullptr;
        }
    } else {
        setProperty("PVGFramebufferModeRepairScheduled", true);
        modeRepairTimer_->setTimeoutMS(1);
    }

    setProperty("PVGFramebufferBootstrapResult",
                static_cast<uint64_t>(result), 32);
    setProperty("PVGFramebufferBootstrapModeCount",
                static_cast<uint64_t>(sizeof(kFallbackModes) /
                                      sizeof(kFallbackModes[0])), 32);
    setProperty("PVGFramebufferBootstrapReannounced", reannouncing);
    registerService();
    return result == kIOReturnSuccess;
}

void PVGFramebufferBootstrap::modeRepairTimerAction(
    OSObject *owner, IOTimerEventSource *sender) {
    static_cast<PVGFramebufferBootstrap *>(owner)->repairMode(sender);
}

void PVGFramebufferBootstrap::repairMode(IOTimerEventSource *sender) {
    constexpr uint32_t kRetryDelayMS = 10;
    constexpr uint32_t kMaximumAttempts = 500;

    ++modeRepairAttempts_;
    setProperty("PVGFramebufferModeRepairAttempts",
                static_cast<uint64_t>(modeRepairAttempts_), 32);

    auto *framebuffer =
        static_cast<AppleParavirtFramebuffer *>(getProvider());
    if (framebuffer == nullptr) {
        setProperty("PVGFramebufferModeRepairProviderMissing", true);
        return;
    }

    // AppleParavirtFramebuffer::start initializes both the startup and current
    // mode IDs to -1. The donor's getDisplayModes implementation exposes the
    // detailed-timing array indices verbatim (0..count-1).
    const uint32_t displayModeCount = framebuffer->getDisplayModeCount();
    setProperty("PVGFramebufferDisplayModeCount",
                static_cast<uint64_t>(displayModeCount), 32);
    if (displayModeCount == 0) {
        if (modeRepairAttempts_ < kMaximumAttempts) {
            sender->setTimeoutMS(kRetryDelayMS);
        } else {
            setProperty("PVGFramebufferModeRepairTimedOut", true);
        }
        return;
    }

    IODisplayModeID currentMode = static_cast<IODisplayModeID>(-1);
    IOIndex currentDepth = 0;
    IOPixelInformation pixelInfo = {};
    IOReturn currentModeResult =
        framebuffer->getCurrentDisplayMode(&currentMode, &currentDepth);
    IOReturn pixelInfoResult = framebuffer->getPixelInformation(
        currentMode, currentDepth, 0, &pixelInfo);

    setProperty("PVGFramebufferCurrentModeBefore",
                static_cast<uint64_t>(currentMode), 32);
    setProperty("PVGFramebufferCurrentDepthBefore",
                static_cast<uint64_t>(currentDepth), 32);
    setProperty("PVGFramebufferCurrentModeResultBefore",
                static_cast<uint64_t>(currentModeResult), 32);
    setProperty("PVGFramebufferPixelInfoResultBefore",
                static_cast<uint64_t>(pixelInfoResult), 32);

    IOReturn startupModeResult = kIOReturnSuccess;
    IOReturn setModeResult = kIOReturnSuccess;
    if (pixelInfoResult != kIOReturnSuccess) {
        startupModeResult = framebuffer->setStartupDisplayMode(0, 2);
        setModeResult = framebuffer->setDisplayMode(0, 2);

        currentMode = static_cast<IODisplayModeID>(-1);
        currentDepth = 0;
        pixelInfo = {};
        currentModeResult =
            framebuffer->getCurrentDisplayMode(&currentMode, &currentDepth);
        pixelInfoResult = framebuffer->getPixelInformation(
            currentMode, currentDepth, 0, &pixelInfo);
    }

    setProperty("PVGFramebufferStartupModeResult",
                static_cast<uint64_t>(startupModeResult), 32);
    setProperty("PVGFramebufferSetModeResult",
                static_cast<uint64_t>(setModeResult), 32);
    setProperty("PVGFramebufferCurrentModeAfter",
                static_cast<uint64_t>(currentMode), 32);
    setProperty("PVGFramebufferCurrentDepthAfter",
                static_cast<uint64_t>(currentDepth), 32);
    setProperty("PVGFramebufferCurrentModeResultAfter",
                static_cast<uint64_t>(currentModeResult), 32);
    setProperty("PVGFramebufferPixelInfoResultAfter",
                static_cast<uint64_t>(pixelInfoResult), 32);
    setProperty("PVGFramebufferModeRepairComplete",
                pixelInfoResult == kIOReturnSuccess);
}

void PVGFramebufferBootstrap::stop(IOService *provider) {
    if (modeRepairTimer_ != nullptr) {
        modeRepairTimer_->cancelTimeout();
        if (modeRepairWorkLoop_ != nullptr) {
            modeRepairWorkLoop_->removeEventSource(modeRepairTimer_);
        }
        modeRepairTimer_->release();
        modeRepairTimer_ = nullptr;
    }
    modeRepairWorkLoop_ = nullptr;
    IOService::stop(provider);
}
