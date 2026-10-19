// XNU 4570 predates kernel Blocks. Reuse Apple's Mojave kernel implementation,
// which uses kern_os_malloc/free (available on High Sierra), not newer kheap.
#define __IGNORE_WCASTALIGN(expression) (expression)
#include "../../research/xnu-4903.270.47/libkern/libclosure/runtime.cpp"
#undef __IGNORE_WCASTALIGN
extern "C" {
#include "../../research/xnu-4903.270.47/libkern/libclosure/libclosuredata.c"
}
#include <libkern/c++/OSCollectionIterator.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSSymbol.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOInterruptEventSource.h>

// Keep the native event source scheduling/interrupt implementation. Only the
// action thunk and copied Block lifetime are supplied by these subclasses.
class PVGHSTimer : public IOTimerEventSource {
    OSDeclareDefaultStructors(PVGHSTimer)
public:
    void *block = nullptr;
    static void fire(OSObject *, IOTimerEventSource *sender) {
        auto self = static_cast<PVGHSTimer *>(sender);
        struct Call { void *isa; int flags, reserved; void (*invoke)(void *, IOTimerEventSource *); };
        static_cast<Call *>(self->block)->invoke(self->block, sender);
    }
    bool start(uint32_t options, OSObject *owner, void *action) {
        block = _Block_copy(action);
        return block && init(options, owner, fire);
    }
    void free() override {
        if (block) _Block_release(block);
        IOTimerEventSource::free();
    }
};
OSDefineMetaClassAndStructors(PVGHSTimer, IOTimerEventSource)
extern "C" __attribute__((used, visibility("default")))
IOTimerEventSource *HighSierraTimer(uint32_t, OSObject *, void *)
    __asm__("__ZN18IOTimerEventSource16timerEventSourceEjP8OSObjectU13block_pointerFvPS_E");
IOTimerEventSource *HighSierraTimer(uint32_t options, OSObject *owner, void *block) {
    auto self = new PVGHSTimer;
    if (self && !self->start(options, owner, block)) { self->release(); return nullptr; }
    return self;
}
class PVGHSInterrupt : public IOInterruptEventSource {
    OSDeclareDefaultStructors(PVGHSInterrupt)
public:
    void *block = nullptr;
    static void fire(OSObject *, IOInterruptEventSource *sender, int count) {
        auto self = static_cast<PVGHSInterrupt *>(sender);
        struct Call { void *isa; int flags, reserved; void (*invoke)(void *, IOInterruptEventSource *, int); };
        static_cast<Call *>(self->block)->invoke(self->block, sender, count);
    }
    bool start(OSObject *owner, IOService *provider, int index, void *action) {
        block = _Block_copy(action);
        return block && init(owner, fire, provider, index);
    }
    void free() override {
        if (block) _Block_release(block);
        IOInterruptEventSource::free();
    }
};
OSDefineMetaClassAndStructors(PVGHSInterrupt, IOInterruptEventSource)
extern "C" __attribute__((used, visibility("default")))
IOInterruptEventSource *HighSierraInterrupt(OSObject *, IOService *, int, void *)
    __asm__("__ZN22IOInterruptEventSource20interruptEventSourceEP8OSObjectP9IOServiceiU13block_pointerFvPS_iE");
IOInterruptEventSource *HighSierraInterrupt(OSObject *owner, IOService *provider, int index, void *block) {
    auto self = new PVGHSInterrupt;
    if (self && !self->start(owner, provider, index, block)) { self->release(); return nullptr; }
    return self;
}

// Blocks have the documented invoke entry at +16 on x86_64. Iterators preserve
// the native mutation check; return false if the collection changes mid-walk.
struct PVGKernelBlock {
    void *isa;
    int flags, reserved;
    bool (*invoke)(void *, ...);
};
static IOReturn HighSierraWorkLoopAction(OSObject *, void *arg, void *, void *, void *) {
    struct ActionBlock { void *isa; int flags, reserved; IOReturn (*invoke)(void *); };
    return static_cast<ActionBlock *>(arg)->invoke(arg);
}
extern "C" __attribute__((used, visibility("default")))
IOReturn HighSierraRunActionBlock(IOWorkLoop *, void *)
    __asm__("__ZN10IOWorkLoop14runActionBlockEU13block_pointerFivE");
IOReturn HighSierraRunActionBlock(IOWorkLoop *self, void *block) {
    return self->runAction(HighSierraWorkLoopAction, self, block);
}
extern "C" __attribute__((used, visibility("default")))
bool HighSierraCollectionIterate(OSCollection *, PVGKernelBlock *)
    __asm__("__ZN12OSCollection14iterateObjectsEU13block_pointerFbP8OSObjectE");
bool HighSierraCollectionIterate(OSCollection *self, PVGKernelBlock *block) {
    auto iter = OSCollectionIterator::withCollection(self);
    if (!iter) return false;
    while (auto object = iter->getNextObject()) {
        if (block->invoke(block, object)) break;
    }
    bool valid = iter->isValid();
    iter->release();
    return valid;
}
extern "C" __attribute__((used, visibility("default")))
bool HighSierraDictionaryIterate(OSDictionary *, PVGKernelBlock *)
    __asm__("__ZN12OSDictionary14iterateObjectsEU13block_pointerFbPK8OSSymbolP8OSObjectE");
bool HighSierraDictionaryIterate(OSDictionary *self, PVGKernelBlock *block) {
    auto iter = OSCollectionIterator::withCollection(self);
    if (!iter) return false;
    while (auto object = iter->getNextObject()) {
        auto key = OSDynamicCast(OSSymbol, object);
        if (!key) { iter->release(); return false; }
        if (block->invoke(block, key, self->getObject(key))) break;
    }
    bool valid = iter->isValid();
    iter->release();
    return valid;
}
