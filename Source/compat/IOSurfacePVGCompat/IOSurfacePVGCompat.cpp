#include <IOKit/IOLib.h>
#include <libkern/c++/OSObject.h>
#include <kern/sched_prim.h>

#include <stdint.h>

namespace {

template <typename T>
inline volatile T &fieldAt(void *object, uintptr_t offset) {
    return *reinterpret_cast<volatile T *>(
        reinterpret_cast<uintptr_t>(object) + offset);
}

template <typename T>
inline const volatile T &fieldAt(const void *object, uintptr_t offset) {
    return *reinterpret_cast<const volatile T *>(
        reinterpret_cast<uintptr_t>(object) + offset);
}

constexpr uintptr_t kWaiterCountOffset = 0x18;
constexpr uintptr_t kCompletedValueOffset = 0x20;
constexpr uintptr_t kTraceIDOffset = 0x28;
constexpr uintptr_t kSharedEventReferenceEventOffset = 0x10;

} // namespace

// These private classes belong to Catalina's IOSurface.kext.  This kext does
// not instantiate or replace them; it supplies methods added in Big Sur that
// operate on fields already present in Catalina 19H15.  The offsets and method
// behavior are verified against both release binaries.
class IOSurfaceSharedEvent {
public:
    bool waitCompleted(uint64_t value, uint64_t deadline);
    void signalCompleted(uint64_t value);
    uint64_t getTraceID() const;
    uint64_t getCompletedValue() const;

    // Existing exported Catalina method.  It wakes waiters and drains event
    // notifications after completedValue advances.
    void doNotify();
};

class IOSurfaceSharedEventReference {
public:
    IOSurfaceSharedEvent *getSharedEvent() const;
};

// AppleParavirtGPU dynamically imports this class-specific allocator.  Big
// Sur resolves it as the OSObject allocator; Catalina has no exported alias.
class IORangeAllocator {
public:
    static void *operator new(size_t size);
};

void *IORangeAllocator::operator new(size_t size) {
    return OSObject::operator new(size);
}

// IOGraphicsFamily 585.2 was compiled after IOService gained a class-specific
// operator new. Catalina 19H15 predates typed heaps: its OSObject allocator is
// the native zeroing kalloc path and the imported paired delete remains
// OSObject::operator delete. Export the newer mangled spelling while retaining
// Catalina's allocator pairing.
extern "C" __attribute__((used, noinline, visibility("default")))
void *IOServiceOperatorNew(size_t size)
    __asm__("__ZN9IOServicenwEm");

extern "C" __attribute__((used, noinline, visibility("default")))
void *IOServiceOperatorNew(size_t size) {
    return OSObject::operator new(size);
}

IOSurfaceSharedEvent *IOSurfaceSharedEventReference::getSharedEvent() const {
    return fieldAt<IOSurfaceSharedEvent *>(
        this, kSharedEventReferenceEventOffset);
}

void IOSurfaceSharedEvent::signalCompleted(uint64_t value) {
    volatile uint64_t &completed = fieldAt<uint64_t>(
        this, kCompletedValueOffset);

    uint64_t observed = completed;
    while (observed < value) {
        if (__sync_bool_compare_and_swap(&completed, observed, value)) {
            doNotify();
            return;
        }
        observed = completed;
    }
}

bool IOSurfaceSharedEvent::waitCompleted(uint64_t value,
                                         uint64_t deadline) {
    volatile uint64_t &completed = fieldAt<uint64_t>(
        this, kCompletedValueOffset);
    volatile int32_t &waiters = fieldAt<int32_t>(
        this, kWaiterCountOffset);

    if (completed >= value) {
        return true;
    }

    __sync_fetch_and_add(&waiters, 1);
    bool completedInTime = true;

    while (completed < value) {
        wait_result_t result = assert_wait_deadline(
            reinterpret_cast<event_t>(this), THREAD_UNINT, deadline);

        if (completed >= value) {
            thread_wakeup_prim(reinterpret_cast<event_t>(this), FALSE,
                               THREAD_AWAKENED);
            break;
        }

        if (result == THREAD_WAITING) {
            result = thread_block(THREAD_CONTINUE_NULL);
        } else if (result == THREAD_AWAKENED) {
            break;
        }

        if (result == THREAD_TIMED_OUT) {
            completedInTime = false;
            break;
        }
    }

    __sync_fetch_and_sub(&waiters, 1);
    return completedInTime;
}

uint64_t IOSurfaceSharedEvent::getTraceID() const {
    return fieldAt<uint64_t>(this, kTraceIDOffset);
}

uint64_t IOSurfaceSharedEvent::getCompletedValue() const {
    return fieldAt<uint64_t>(this, kCompletedValueOffset);
}
