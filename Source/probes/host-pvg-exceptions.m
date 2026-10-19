// Host-only diagnostic interposer. Logs the caught PVG exception before Apple's
// unified log redacts its reason. It does not suppress or change exceptions.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <objc/runtime.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <stdio.h>

static IMP originalNoCopy;
static id traceNoCopy(id self, SEL selector, void *bytes, NSUInteger length,
                      MTLResourceOptions options, id deallocator)
{
    id result = ((id (*)(id, SEL, void *, NSUInteger, MTLResourceOptions, id))
                 originalNoCopy)(self, selector, bytes, length, options, deallocator);
    static unsigned failures;
    if (!result && __atomic_fetch_add(&failures, 1, __ATOMIC_RELAXED) < 32) {
        mach_vm_address_t address = (mach_vm_address_t)bytes;
        mach_vm_size_t size = 0;
        vm_region_basic_info_data_64_t info = {};
        mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t object = MACH_PORT_NULL;
        kern_return_t kr = mach_vm_region(mach_task_self(), &address, &size,
            VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &count, &object);
        fprintf(stderr, "PVG no-copy failed pointer=%p length=0x%lx options=0x%lx region=0x%llx size=0x%llx prot=%d max=%d kr=%d\n",
            bytes, length, options, address, size, info.protection, info.max_protection, kr);
        if (object) mach_port_deallocate(mach_task_self(), object);
    }
    return result;
}

__attribute__((constructor)) static void installNoCopyTrace(void)
{
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    SEL selector = @selector(newBufferWithBytesNoCopy:length:options:deallocator:);
    Class cls = object_getClass(device);
    Method method = class_getInstanceMethod(cls, selector);
    if (method) {
        originalNoCopy = method_getImplementation(method);
        class_replaceMethod(cls, selector, (IMP)traceNoCopy, method_getTypeEncoding(method));
    }
    [device release];
}

extern void objc_exception_throw(id exception);
static void pvg_exception_throw(id exception)
{
    static _Thread_local bool logging;
    static unsigned reports;
    // A failed channel can throw for every queued command. Keep diagnostics
    // bounded so a compatibility failure does not become a logging bottleneck.
    if (!logging && __atomic_fetch_add(&reports, 1, __ATOMIC_RELAXED) < 16) {
      logging = true;
      @try {
        fprintf(stderr, "PVG host exception: %s: %s\n",
                [[exception name] UTF8String], [[exception reason] UTF8String]);
        void *stack[32];
        int count = backtrace(stack, 32);
        backtrace_symbols_fd(stack, count, 2);
      } @catch (...) {
        fprintf(stderr, "PVG exception description unavailable\n");
      }
      logging = false;
    }
    // dyld excludes the interposing image's own bound reference. RTLD_NEXT
    // lookup on macOS 27 instead returned the interposed entry recursively.
    objc_exception_throw(exception);
    __builtin_unreachable();
}
__attribute__((used, section("__DATA,__interpose")))
static const struct { const void *replacement, *original; } hook = {
    (const void *)pvg_exception_throw, (const void *)objc_exception_throw
};
