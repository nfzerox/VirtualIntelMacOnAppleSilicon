#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSString.h>
#ifdef PVG_RUNTIME_BOOT_POLICY
#include <pexpert/pexpert.h>
#endif

// Driver registration service, retaining its bring-up name for compatibility.
// No interposition or GPU hot-path hook: advertise Apple's renderer normally.
class PVGOpenGLProbe : public IOService {
    OSDeclareDefaultStructors(PVGOpenGLProbe)
    OSObject *previous_ = nullptr;
public:
    bool start(IOService *provider) override {
#ifdef PVG_RUNTIME_BOOT_POLICY
        // The immutable Big Sur EFI contains this driver during installation
        // too. Do not advertise a renderer before its guest bundles exist.
        int enabled = 0;
        if (!PE_parse_boot_argn("vim_gl", &enabled, sizeof(enabled)) || enabled != 1)
            return false;
#endif
        if (!IOService::start(provider)) return false;
        // Legacy PCI clients (including hardware inventory tools) consume
        // "model" as a NUL-terminated CFData, not CFString. Normalize only
        // this accelerator's PCI ancestor; retain the donor's exact name.
        for (IORegistryEntry *entry = provider; entry;
             entry = entry->getParentEntry(gIOServicePlane)) {
            if (!entry->metaCast("IOPCIDevice")) continue;
            OSString *model = OSDynamicCast(OSString, entry->getProperty("model"));
            if (model) {
                OSData *data = OSData::withBytes(model->getCStringNoCopy(),
                                                model->getLength() + 1);
                if (data) {
                    entry->setProperty("model", data);
                    data->release();
                }
            }
            break;
        }
        previous_ = provider->getProperty("IOGLBundleName");
        if (previous_) previous_->retain();
        if (!provider->setProperty("IOGLBundleName", "AppleMetalOpenGLRenderer")) {
            OSSafeReleaseNULL(previous_);
            return false;
        }
        IOLog("PVGOpenGLProbe: published AppleMetalOpenGLRenderer\n");
        return true;
    }
    void stop(IOService *provider) override {
        if (previous_) provider->setProperty("IOGLBundleName", previous_);
        else provider->removeProperty("IOGLBundleName");
        OSSafeReleaseNULL(previous_);
        IOService::stop(provider);
    }
};
OSDefineMetaClassAndStructors(PVGOpenGLProbe, IOService)
