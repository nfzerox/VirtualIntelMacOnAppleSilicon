#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IORegistryEntry.h>
#include <libkern/c++/OSMetaClass.h>
#include <libkern/c++/OSObject.h>
#include <mach/vm_param.h>

#include <stdint.h>

#if PVG_HIGH_SIERRA
#include "PVGHighSierraKernel.inc"
#endif

#if PVG_MOJAVE
#include "PVGMojaveKernel.inc"
#endif

// This kext supplies the small set of kernel interfaces imported by the
// macOS 11.7 IOSurface/IOGraphics/PVG binaries but absent from Catalina
// 19H15. Every adapter below either delegates to Catalina's predecessor API
// or implements the corresponding xnu-7195 behavior using Catalina's public
// IOKit ABI. It intentionally has no dependency on IOSurface, which lets the
// donor IOSurface depend on this bundle without a circular dependency.

namespace {

constexpr IOOptionBits kIOMemorySourceSegment = 0x00002000;

} // namespace

// AppleParavirtGPU imports this class-specific allocator. Big Sur resolves it
// as the ordinary OSObject allocator; Catalina has no exported alias.
class IORangeAllocator {
public:
    static void *operator new(size_t size);
};

void *IORangeAllocator::operator new(size_t size) {
    return OSObject::operator new(size);
}

// IOGraphicsFamily 585.2 imports the IOService-specific spelling introduced
// after Catalina. Catalina's OSObject allocator is the correct paired
// allocator for this binary (its delete import is OSObject::operator delete).
extern "C" __attribute__((used, noinline, visibility("default")))
void *IOServiceOperatorNew(size_t size)
    __asm__("__ZN9IOServicenwEm");

extern "C" __attribute__((used, noinline, visibility("default")))
void *IOServiceOperatorNew(size_t size) {
    return OSObject::operator new(size);
}

// Big Sur's zoned metaclass constructor delegates to the pre-zone constructor
// before optionally creating a typed zone. Catalina has the delegated
// constructor but not typed IOKit heaps. Leave the caller's zone null: the
// generated Big Sur operator new/delete paths then deliberately use the
// Catalina OSObject allocator pair.
extern "C" void CatalinaOSMetaClassConstructor(
    OSMetaClass *self,
    const char *className,
    const OSMetaClass *superClass,
    unsigned int classSize)
    __asm__("__ZN11OSMetaClassC2EPKcPKS_j");

extern "C" __attribute__((used, noinline, visibility("default")))
void BigSurOSMetaClassConstructor(
    OSMetaClass *self,
    const char *className,
    const OSMetaClass *superClass,
    unsigned int classSize,
    void **classZone,
    const char *zoneName,
    uint64_t zoneFlags)
    __asm__("__ZN11OSMetaClassC2EPKcPKS_jPP4zoneS1_19zone_create_flags_t");

extern "C" __attribute__((used, noinline, visibility("default")))
void BigSurOSMetaClassConstructor(
    OSMetaClass *self,
    const char *className,
    const OSMetaClass *superClass,
    unsigned int classSize,
    void **classZone,
    const char *zoneName,
    uint64_t zoneFlags) {
    (void)zoneName;
    (void)zoneFlags;
    if (classZone) {
        *classZone = nullptr;
    }
    CatalinaOSMetaClassConstructor(self, className, superClass, classSize);
}

// Catalina predates the convenience method but exposes the same virtual
// physical-segment API. This is the xnu-7195 fallback implementation for
// descriptors without the later internal memory-reference representation.
extern "C" __attribute__((used, noinline, visibility("default")))
uint64_t BigSurGetDMAMapLength(
    IOMemoryDescriptor *descriptor,
    uint64_t *firstPageOffset)
    __asm__("__ZN18IOMemoryDescriptor15getDMAMapLengthEPy");

extern "C" __attribute__((used, noinline, visibility("default")))
uint64_t BigSurGetDMAMapLength(
    IOMemoryDescriptor *descriptor,
    uint64_t *firstPageOffset) {
    uint64_t totalLength = 0;
    IOByteCount iterate = 0;

    if (firstPageOffset) {
        *firstPageOffset = 0;
    }

    while (iterate < descriptor->getLength()) {
        IOByteCount segmentLength = 0;
        const addr64_t sourceAddress = descriptor->getPhysicalSegment(
            iterate, &segmentLength, kIOMemorySourceSegment);
        if (!sourceAddress || !segmentLength) {
            break;
        }

        if (firstPageOffset && iterate == 0) {
            *firstPageOffset = sourceAddress & PAGE_MASK_64;
        }

        const uint64_t pageStart = sourceAddress & ~PAGE_MASK_64;
        const uint64_t pageEnd =
            (sourceAddress + segmentLength + PAGE_MASK_64) & ~PAGE_MASK_64;
        totalLength += pageEnd - pageStart;
        iterate += segmentLength;
    }

    return totalLength;
}

// In xnu-7195 descriptor IDs exist only to correlate kernel trace records.
// Catalina has no reserved field in which to cache the new counter, so a
// stable non-zero per-object value preserves the functional contract without
// touching the incompatible private object layout.
extern "C" __attribute__((used, noinline, visibility("default")))
uint64_t BigSurGetDescriptorID(IOMemoryDescriptor *descriptor)
    __asm__("__ZN18IOMemoryDescriptor15getDescriptorIDEv");

extern "C" __attribute__((used, noinline, visibility("default")))
uint64_t BigSurGetDescriptorID(IOMemoryDescriptor *descriptor) {
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(descriptor));
}

// This API emits optional kdebug diagnostics only. Returning success preserves
// behavior when tracing is disabled, which is the first branch in xnu-7195.
extern "C" __attribute__((used, noinline, visibility("default")))
int32_t BigSurKtraceEmitPhysicalSegments(IOMemoryDescriptor *descriptor)
    __asm__("__ZN18IOMemoryDescriptor26ktraceEmitPhysicalSegmentsEv");

extern "C" __attribute__((used, noinline, visibility("default")))
int32_t BigSurKtraceEmitPhysicalSegments(IOMemoryDescriptor *descriptor) {
    (void)descriptor;
    return kIOReturnSuccess;
}

// xnu-7195 implements this method as a one-line getProperty predicate.
extern "C" __attribute__((used, noinline, visibility("default")))
bool BigSurPropertyExists(
    const IORegistryEntry *entry,
    const char *key,
    const IORegistryPlane *plane,
    uint32_t options)
    __asm__("__ZNK15IORegistryEntry14propertyExistsEPKcPK15IORegistryPlanej");

extern "C" __attribute__((used, noinline, visibility("default")))
bool BigSurPropertyExists(
    const IORegistryEntry *entry,
    const char *key,
    const IORegistryPlane *plane,
    uint32_t options) {
    return entry->getProperty(key, plane, options) != nullptr;
}

// These two allocator entry points remain imports in the donor binary's
// generated zoned operator methods. The metaclass bridge above guarantees its
// zone pointer remains null, so normal execution cannot reach either wrapper.
// Returning null for an impossible allocation and leaking rather than freeing
// through an unknown zone are the only safe fallbacks without private KPIs.

extern "C" __attribute__((used, noinline, visibility("default")))
void *BigSurZallocFlags(void *zone, uint32_t flags)
    __asm__("_zalloc_flags");

extern "C" __attribute__((used, noinline, visibility("default")))
void *BigSurZallocFlags(void *zone, uint32_t flags) {
    // No allocation size is present in this ABI, so there is no honest
    // Catalina heap fallback. The only donor call site is guarded by the zone
    // pointer that BigSurOSMetaClassConstructor always leaves null.
    (void)zone;
    (void)flags;
    return nullptr;
}

extern "C" __attribute__((used, noinline, visibility("default")))
void BigSurKernOSZfree(void *zone, void *address, uint64_t size)
    __asm__("_kern_os_zfree");

extern "C" __attribute__((used, noinline, visibility("default")))
void BigSurKernOSZfree(void *zone, void *address, uint64_t size) {
    (void)zone;
    (void)address;
    (void)size;
}
