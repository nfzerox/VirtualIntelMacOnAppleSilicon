// Mojave-only exports. Never build these into the Catalina compatibility kext.
// Mojave's KPI export aliases already supply IOLock*, IOSimpleLock* and
// OSSerialize::binaryWithCapacity(void*,void*). A raw kernel nm audit misses
// these; adding wrappers produces duplicate definitions in guest kextutil.
#include <IOKit/IOBufferMemoryDescriptor.h>

#define PVG_EXPORT __attribute__((used, noinline, visibility("default")))
#define PVG_ALIAS(result, name, args, symbol) \
    extern "C" PVG_EXPORT result name args __asm__(symbol); \
    extern "C" PVG_EXPORT result name args

PVG_ALIAS(void *, MojaveIOMallocZero, (vm_size_t size), "_IOMallocZero") {
    void *result = IOMalloc(size);
    if (result) bzero(result, size);
    return result;
}

// Native 18G103 contains, but does not export through a KPI, the ushort
// method. Exact release disassembly at ffffff8000a71de0 is two word stores
// at +0x30 and +0x32; xnu-4903's private header confirms the two vm_tag_t
// fields. This candidate is for verified 18G103; recheck for other kernels.
PVG_ALIAS(void, MojaveSetVMTags, (void *self, uint32_t kernel, uint32_t user),
    "__ZN18IOMemoryDescriptor9setVMTagsEjj") {
    auto bytes = static_cast<uint8_t *>(self);
    *reinterpret_cast<uint16_t *>(bytes + 0x30) = static_cast<uint16_t>(kernel);
    *reinterpret_cast<uint16_t *>(bytes + 0x32) = static_cast<uint16_t>(user);
}

// xnu-7195's six-argument overload: apply the VM tags before allocation,
// then initialize with the native Mojave virtual method and release on error.
PVG_ALIAS(IOBufferMemoryDescriptor *, MojaveTaggedBuffer,
    (task_t task, IOOptionBits options, vm_size_t size, vm_offset_t alignment,
     uint32_t kernelTag, uint32_t userTag),
    "__ZN24IOBufferMemoryDescriptor17inTaskWithOptionsEP4taskjmmjj") {
    auto result = new IOBufferMemoryDescriptor;
    if (result) {
        MojaveSetVMTags(result, kernelTag, userTag);
        if (!result->initWithPhysicalMask(task, options, size, alignment, 0)) {
            result->release();
            result = nullptr;
        }
    }
    return result;
}

// Mojave has no IOMemoryDescriptor ownership/ledger-transfer implementation.
// Report the same unavailable-feature result as xnu-7195 with its ownership
// facility disabled. Do not claim that accounting was transferred.
PVG_ALIAS(IOReturn, MojaveSetOwnership, (void *, void *, int, unsigned),
    "__ZN18IOMemoryDescriptor12setOwnershipEP4taskij") {
    return kIOReturnUnsupported;
}
PVG_ALIAS(IOReturn, MojaveGeneralSetOwnership, (void *, void *, int, unsigned),
    "__ZN25IOGeneralMemoryDescriptor12setOwnershipEP4taskij") {
    return kIOReturnUnsupported;
}

// Donor IOSurface uses this exclusively to gate optional dev-fused surface
// tracing (verified callsite 0x897e), not rendering or synchronization.
PVG_ALIAS(bool, MojaveExplicitDebugEnabled, (uint32_t),
    "_kdebug_debugid_explicitly_enabled") {
    return false;
}

// xnu-4903 RELEASE's os_refcnt is the same four-byte atomic counter as
// xnu-7195's os_ref_atomic_t. Group arguments are diagnostic-only on RELEASE.
// This candidate targets the verified RELEASE_X86_64 guest, not a DEBUG KDK.
#if PVG_HIGH_SIERRA
// XNU 4570 has no os_ref API. Implement the donor RELEASE counter semantics
// from xnu-7195.141.2/libkern/os/refcnt.c (four-byte atomic, no debug group).
static void os_ref_init_count(void *p, void *, uint32_t n) {
    __atomic_store_n(static_cast<uint32_t *>(p), n, __ATOMIC_RELAXED);
}
static void os_ref_retain(void *p) {
    uint32_t old = __atomic_fetch_add(static_cast<uint32_t *>(p), 1, __ATOMIC_RELAXED);
    if (!old || old >= 0x0fffffff) panic("PVG: invalid reference retain");
}
static bool os_ref_retain_try(void *p) {
    auto counter = static_cast<uint32_t *>(p);
    uint32_t old = __atomic_load_n(counter, __ATOMIC_RELAXED);
    do {
        if (!old) return false;
        if (old >= 0x0fffffff) panic("PVG: reference overflow");
    } while (!__atomic_compare_exchange_n(counter, &old, old + 1, true,
                                         __ATOMIC_RELAXED, __ATOMIC_RELAXED));
    return true;
}
static uint32_t os_ref_release_explicit(void *p, int, int) {
    auto counter = static_cast<uint32_t *>(p);
    uint32_t old = __atomic_fetch_sub(counter, 1, __ATOMIC_RELEASE);
    if (!old) panic("PVG: reference underflow");
    if (old == 1) (void)__atomic_load_n(counter, __ATOMIC_ACQUIRE);
    return old - 1;
}
#else
extern "C" void os_ref_init_count(void *, void *, uint32_t);
extern "C" void os_ref_retain(void *);
extern "C" bool os_ref_retain_try(void *);
extern "C" uint32_t os_ref_release_explicit(void *, int, int);
#endif
PVG_ALIAS(void, MojaveRefInit, (void *counter, void *group, uint32_t count),
    "_os_ref_init_count_external") {
    if (!count) panic("PVG: initializing a zero reference count");
    os_ref_init_count(counter, group, count);
}
PVG_ALIAS(void, MojaveRefRetain, (void *counter, void *),
    "_os_ref_retain_external") {
    os_ref_retain(counter);
}
PVG_ALIAS(bool, MojaveRefRetainTry, (void *counter, void *),
    "_os_ref_retain_try_external") {
    return os_ref_retain_try(counter);
}
PVG_ALIAS(uint32_t, MojaveRefRelease, (void *counter, void *),
    "_os_ref_release_barrier_external") {
    return os_ref_release_explicit(counter, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE);
}

// xnu-4903 thread.c: only sets the BSD diagnostic thread name. No scheduling
// or synchronization effects. Keep threads functional without private fields.
PVG_ALIAS(void, MojaveThreadName, (void *, const char *),
    "_thread_set_thread_name") {}

#undef PVG_ALIAS
#undef PVG_EXPORT
