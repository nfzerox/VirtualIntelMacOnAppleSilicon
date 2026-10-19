// ARM26 diagnostic only. Offsets verified in this donor's disassembly.
// Never changes objects or suppresses the original exception.
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <stdio.h>
static IMP original;
static BOOL readBytes(uint64_t address, void *out, size_t count) {
    mach_vm_size_t actual = 0;
    return mach_vm_read_overwrite(mach_task_self(), address, count,
        (mach_vm_address_t)out, &actual) == KERN_SUCCESS && actual == count;
}
static id trace(id self, SEL selector, uint32_t reference) {
    @try {
        return ((id (*)(id, SEL, uint32_t))original)(self, selector, reference);
    } @catch (NSException *exception) {
        static unsigned reports;
        if (__atomic_fetch_add(&reports, 1, __ATOMIC_RELAXED) < 8) {
            uint64_t manager = 0, list = 0, mapped = 0, guestOffset = 0;
            uint32_t count = 0, bytes = 0, entry[3] = {};
            BOOL ok = readBytes((uint64_t)self + 0x28, &manager, 8) &&
                readBytes(manager + 0x18, &list, 8) &&
                readBytes(list + 0x58, &mapped, 8) &&
                readBytes(list + 0x68, &guestOffset, 8) &&
                readBytes(list + 0x70, &bytes, 4) &&
                readBytes(list + 0x74, &count, 4);
            BOOL got = ok && reference < count &&
                readBytes(mapped + (uint64_t)reference * 12, entry, 12);
            fprintf(stderr, "PVG object trace task=%p ref=%u list=0x%llx gva=0x%llx mapped=0x%llx bytes=%u count=%u readable=%d entry=%08x:%08x:%08x\n",
                self, reference, list, guestOffset, mapped, bytes, count, got,
                entry[0], entry[1], entry[2]);
            @try {
                // Read the same guest physical bytes through the donor's
                // physical-read callback, bypassing the task virtual alias.
                Ivar ti = class_getInstanceVariable([self class], "_pageTableIterator");
                id iterator = ti ? object_getIvar(self, ti) : nil;
                Class cls = [iterator class];
                Ivar ri = class_getInstanceVariable(cls, "_root");
                Ivar di = class_getInstanceVariable(cls, "_depth");
                Ivar bi = class_getInstanceVariable(cls, "_read");
                uint64_t root = 0;
                unsigned depth = 0;
                if (iterator && ri && di && bi &&
                    readBytes((uint64_t)iterator + ivar_getOffset(ri), &root, 8) &&
                    readBytes((uint64_t)iterator + ivar_getOffset(di), &depth, 4) &&
                    depth && depth <= 6 && got) {
                    BOOL (^read)(uint64_t, uint64_t, void *) = object_getIvar(iterator, bi);
                    uint64_t va = guestOffset + (uint64_t)reference * 12;
                    uint32_t page = (uint32_t)(root >> 14);
                    for (unsigned level = depth; level && page; --level) {
                        uint64_t slot = ((uint64_t)page << 12) +
                            (((va >> (12 + (level - 1) * 10)) & 1023) * 4);
                        uint32_t pte = 0;
                        if (!read(slot, 4, &pte)) {
                            fprintf(stderr, "PVG object PTE read FAILED level=%u address=0x%llx\n", level, slot);
                            page = 0;
                            break;
                        }
                        fprintf(stderr, "PVG object PTE level=%u address=0x%llx value=%08x\n", level, slot, pte);
                        page = pte & 0x7fffffff;
                    }
                    if (page && (va & 4095) <= 4096 - sizeof(entry)) {
                        uint32_t physical[3] = {};
                        uint64_t address = ((uint64_t)page << 12) + (va & 4095);
                        BOOL physicalOK = read(address, sizeof(physical), physical);
                        fprintf(stderr, "PVG object physical address=0x%llx readOK=%d entry=%08x:%08x:%08x\n",
                            address, physicalOK, physical[0], physical[1], physical[2]);
                        // Bounded context distinguishes an empty page from a
                        // populated list whose individual slot is invalid.
                        uint32_t context[64] = {};
                        BOOL contextOK = read((uint64_t)page << 12, sizeof(context), context);
                        fprintf(stderr, "PVG object physical page prefix readOK=%d:", contextOK);
                        for (unsigned i = 0; i < 64; ++i)
                            fprintf(stderr, " %08x", context[i]);
                        fprintf(stderr, "\n");
                    }
                }
            } @catch (NSException *diagnosticException) {
                fprintf(stderr, "PVG object physical read failed: %s\n", diagnosticException.reason.UTF8String);
            }
        }
        @throw;
    }
}
__attribute__((constructor)) static void install(void) {
    Class cls = NSClassFromString(@"PGBaseTask");
    Method method = class_getInstanceMethod(cls, sel_registerName("getHostResource:"));
    if (!method) abort();
    original = method_getImplementation(method);
    class_replaceMethod(cls, method_getName(method), (IMP)trace,
                        method_getTypeEncoding(method));
}
