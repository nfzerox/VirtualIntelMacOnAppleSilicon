#import <Foundation/Foundation.h>
#import <IOSurface/IOSurface.h>
#import <Metal/Metal.h>
#include "../PVGAIRTarget.h"
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 101500 && PVG_EXPERIMENTAL_LEGACY_AIR
#include "../PVGAIRMetadata.h"
#define PVG_REPAIR_LEGACY_AIR 1
#else
#define PVG_REPAIR_LEGACY_AIR 0
#endif

#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/mach_time.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <os/log.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "PVGCompletionSite.h"
#include "PVGShaderCache.h"

#ifndef PVG_FORCE_DISPLAY_SURFACE_SYNC
#define PVG_FORCE_DISPLAY_SURFACE_SYNC 0
#endif

static BOOL pvgCompilerTraceEnabled(void) {
#if PVG_TRACE_COMPILER_ALWAYS
    return YES;
#else
    return getenv("PVG_TRACE_COMPILER") != NULL;
#endif
}

static void pvgCompilerTrace(const char *event) {
    if (!pvgCompilerTraceEnabled()) return;
    char path[96];
    snprintf(path, sizeof(path), "/tmp/pvg-compiler-%d.log", getpid());
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd >= 0) {
        (void)write(fd, event, strlen(event));
        (void)write(fd, "\n", 1);
        close(fd);
    }
#if PVG_TRACE_COMPILER_ALWAYS
    // WindowServer's normal boot sandbox denies file creation in /tmp. Keep
    // the file sink for unsandboxed probes, and mirror trace builds to the
    // system log so early compositor failures remain observable.
    os_log_with_type(OS_LOG_DEFAULT, OS_LOG_TYPE_ERROR,
                     "PVGCOMPILER %{public}s", event);
#endif
}

typedef id (*MTLResourceInitFn)(id, SEL, id, id, unsigned long, void *,
                                unsigned int);
typedef void *(*IOAccelResourceCreateFn)(void *, void *, uint32_t);
typedef kern_return_t (*IOConnectCallStructMethodFn)(mach_port_t, uint32_t,
                                                     const void *, size_t,
                                                     void *, size_t *);
typedef kern_return_t (*IOAccelDisplayPipeTransactionEndFn)(void *);
typedef kern_return_t (*IOAccelDisplayPipeTransactionSetTimestampFn)(
    void *, uint64_t);
typedef kern_return_t (*IOPresentmentEndFn)(void *, void *, uint64_t, bool);
typedef kern_return_t (*CoreDisplayPresentDisplaySurfaceFn)(void *, void *,
                                                             uint32_t);
typedef IOSurfaceRef (*CoreDisplaySurfaceGetIOSurfaceFn)(void *);

static MTLResourceInitFn originalResourceInit;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 101500
typedef id (*PVGMojaveResourceInitFn)(id, SEL, id, unsigned long, void *, unsigned int);
static PVGMojaveResourceInitFn originalMojaveResourceInit;
#endif
static __thread NSUInteger pvgCurrentTextureUsage;
static __thread MTLTextureType pvgCurrentTextureType;
static __thread MTLTextureDescriptor *pvgCurrentTextureDescriptor;
static __thread NSUInteger pvgCurrentTexturePlane;
static __thread BOOL pvgHasTextureDescriptor;
static __thread unsigned int pvgResourceABITranslationDepth;

#if PVG_TRACE_RESOURCES
static uint32_t pvgResourceTraceCount;

static void pvgTraceResource(const char *phase, uint32_t sequence, id result,
                             id remoteStorageResource, unsigned long options,
                             const void *args, unsigned int argsSize) {
    if (sequence > 4096 || !args)
        return;

    char path[96];
    snprintf(path, sizeof(path), "/tmp/pvg-resource-%d.log", getpid());
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0666);

    uint64_t qword[21] = {0};
    size_t copyLength = argsSize < sizeof(qword) ? argsSize : sizeof(qword);
    memcpy(qword, args, copyLength);
    char line[1024];
    int length = snprintf(
        line, sizeof(line),
        "%s n=%u pid=%d result=%p remote=%p remoteClass=%s options=0x%lx "
        "size=0x%x textureUsage=%s0x%lx textureType=%s0x%lx q="
        "%016llx,%016llx,%016llx,%016llx,%016llx,%016llx,%016llx,"
        "%016llx,%016llx,%016llx,%016llx,%016llx,%016llx,%016llx,"
        "%016llx,%016llx,%016llx,%016llx,%016llx,%016llx,%016llx\n",
        phase, sequence, getpid(), result, remoteStorageResource,
        remoteStorageResource ? object_getClassName(remoteStorageResource)
                              : "(null)",
        options, argsSize, pvgHasTextureDescriptor ? "" : "(unset)",
        (unsigned long)pvgCurrentTextureUsage,
        pvgHasTextureDescriptor ? "" : "(unset)",
        (unsigned long)pvgCurrentTextureType,
        qword[0], qword[1], qword[2], qword[3], qword[4],
        qword[5], qword[6], qword[7], qword[8], qword[9], qword[10],
        qword[11], qword[12], qword[13], qword[14], qword[15], qword[16],
        qword[17], qword[18], qword[19], qword[20]);
    if (fd >= 0 && length > 0)
        (void)write(fd, line,
                    (size_t)length < sizeof(line) ? (size_t)length
                                                  : sizeof(line) - 1);
    if (fd >= 0)
        close(fd);
    if (length > 0)
        os_log_with_type(OS_LOG_DEFAULT, OS_LOG_TYPE_ERROR,
                         "PVGRESOURCE %{public}s", line);
}
#endif

static IOAccelResourceCreateFn originalIOAccelResourceCreate;
static IOConnectCallStructMethodFn originalIOConnectCallStructMethod;
static IOAccelDisplayPipeTransactionEndFn
    originalIOAccelDisplayPipeTransactionEnd;
static IOAccelDisplayPipeTransactionSetTimestampFn
    ioAccelDisplayPipeTransactionSetTimestamp;
static IOPresentmentEndFn originalIOPresentmentEnd;
static CoreDisplayPresentDisplaySurfaceFn
    originalCoreDisplayPresentDisplaySurface;
static __thread uint64_t pvgPresentationTimestamp;

/*
 * Big Sur's IOAccelerator kernel sends the legacy display-pipe completion
 * value as one 64-bit word at record offset 0x18.  Catalina 19H15's private
 * callback instead joins the low halves of the words at 0x18 and 0x20.  The
 * latter truncates the high 32 bits of every PVG completion timestamp and can
 * recycle a scanout surface while the host still owns it.
 *
 * The callback is private/local and its address has already been registered
 * with the notification port, so normal symbol interposition cannot replace
 * it.  Patch only the exact 19H15 instruction sequence, in the process-local
 * COW copy of the shared-cache page.  A signature mismatch fails closed.
 */
static BOOL pvgInstallIOAccelDisplayCompletionABIRepair(void) {
    static const uint8_t catalina19H15JoinWords[] = {
        0x8b, 0x4a, 0x18,             // movl 0x18(%rdx), %ecx
        0x48, 0x8b, 0x72, 0x20,       // movq 0x20(%rdx), %rsi
        0x48, 0xc1, 0xe6, 0x20,       // shlq $32, %rsi
        0x48, 0x09, 0xce              // orq %rcx, %rsi
    };
    static const uint8_t bigSurReadQword[] = {
        0x48, 0x8b, 0x72, 0x18,       // movq 0x18(%rdx), %rsi
        0x90, 0x90, 0x90, 0x90, 0x90,
        0x90, 0x90, 0x90, 0x90, 0x90
    };

    uint8_t *setNotifyPort = (uint8_t *)dlsym(
        RTLD_DEFAULT, "IOAccelDisplayPipeSetTransactionNotifyPort");
    if (!setNotifyPort) {
        os_log_with_type(OS_LOG_DEFAULT, OS_LOG_TYPE_ERROR,
                         "PVG display completion repair: IOAccelerator "
                         "entry point is unavailable");
        return NO;
    }

    Dl_info imageInfo;
    if (!dladdr(setNotifyPort, &imageInfo)) return NO;
    uint8_t *site = pvgFindCompletionSite(imageInfo.dli_fbase);
    if (!site) {
        os_log_with_type(OS_LOG_DEFAULT, OS_LOG_TYPE_ERROR,
                         "PVG display completion repair: no unique native signature");
        return NO;
    }
    if (memcmp(site, bigSurReadQword, sizeof(bigSurReadQword)) == 0)
        return YES;
    if (memcmp(site, catalina19H15JoinWords,
               sizeof(catalina19H15JoinWords)) != 0) {
        os_log_with_type(OS_LOG_DEFAULT, OS_LOG_TYPE_ERROR,
                         "PVG display completion repair: refusing unknown "
                         "IOAccelerator callback at %{public}p", site);
        return NO;
    }

    mach_vm_address_t page = (mach_vm_address_t)(uintptr_t)site &
        ~((mach_vm_address_t)vm_page_size - 1);
    mach_vm_size_t span = (((mach_vm_address_t)(uintptr_t)site +
        sizeof(bigSurReadQword) + vm_page_size - 1) &
        ~((mach_vm_address_t)vm_page_size - 1)) - page;
    kern_return_t result = mach_vm_protect(
        mach_task_self(), page, span, FALSE,
        VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
    if (result != KERN_SUCCESS) {
        os_log_with_type(OS_LOG_DEFAULT, OS_LOG_TYPE_ERROR,
                         "PVG display completion repair: writable COW page "
                         "failed: 0x%x", result);
        return NO;
    }

    memcpy(site, bigSurReadQword, sizeof(bigSurReadQword));
    __builtin___clear_cache((char *)site,
                            (char *)site + sizeof(bigSurReadQword));
    result = mach_vm_protect(mach_task_self(), page, span, FALSE,
                             VM_PROT_READ | VM_PROT_EXECUTE);
    if (result != KERN_SUCCESS) {
        os_log_with_type(OS_LOG_DEFAULT, OS_LOG_TYPE_ERROR,
                         "PVG display completion repair: restoring RX page "
                         "failed: 0x%x", result);
        return NO;
    }

    os_log_with_type(OS_LOG_DEFAULT, OS_LOG_TYPE_DEFAULT,
                     "PVG display completion repair installed");
    return YES;
}

static void pvgTranslateResourceArgsToBigSur(void *args, uint32_t argsSize) {
    if (!args || argsSize < 0x68)
        return;

    uint8_t *bytes = (uint8_t *)args;
    uint32_t resourceType = *(uint32_t *)(bytes + 0x00);
    uint64_t sysMemSize = *(uint64_t *)(bytes + 0x50);
    uint64_t bigSurSysMemSize = *(uint64_t *)(bytes + 0x58);

    if ((resourceType == 0x80 || resourceType == 0xc0) &&
        *(uint64_t *)(bytes + 0x38) != 0) {
        // Catalina has now allocated/mapped any client storage and filled the
        // complete old-layout tuple. Big Sur inserted one qword before it, so
        // shift the finished tuple only in the copy sent to the donor kernel.
        uint64_t clientAddress = *(uint64_t *)(bytes + 0x38);
        uint64_t clientAddressEnd = *(uint64_t *)(bytes + 0x40);
        uint64_t clientOffset = *(uint64_t *)(bytes + 0x48);
        uint64_t clientFlags = *(uint64_t *)(bytes + 0x58);
        *(uint64_t *)(bytes + 0x38) = 0;
        *(uint64_t *)(bytes + 0x40) = clientAddress;
        *(uint64_t *)(bytes + 0x48) = clientAddressEnd;
        *(uint64_t *)(bytes + 0x50) = clientOffset;
        *(uint64_t *)(bytes + 0x58) = sysMemSize;
        *(uint64_t *)(bytes + 0x60) = clientFlags;
    } else if (resourceType == 0x82) {
        uint32_t catalinaSurfaceID = *(uint32_t *)(bytes + 0x38);
        uint32_t bigSurSurfaceID = *(uint32_t *)(bytes + 0x40);
        if (catalinaSurfaceID != 0 && bigSurSurfaceID == 0) {
            uint32_t plane = *(uint32_t *)(bytes + 0x3c);
            uint32_t usage = *(uint32_t *)(bytes + 0x40);
            *(uint64_t *)(bytes + 0x38) = 0;
            *(uint32_t *)(bytes + 0x40) = catalinaSurfaceID;
            *(uint32_t *)(bytes + 0x44) = plane;
            *(uint32_t *)(bytes + 0x48) = usage;
        }
    }

    // AppleParavirt-created descriptors already use +0x58. Only translate a
    // non-client Catalina descriptor when its old size field is populated and
    // the Big Sur field is still empty.
    if (resourceType != 0x80 && resourceType != 0xc0 &&
        sysMemSize != 0 && bigSurSysMemSize == 0) {
        *(uint64_t *)(bytes + 0x50) = 0;
        *(uint64_t *)(bytes + 0x58) = sysMemSize;
        *(uint64_t *)(bytes + 0x60) = 0;
    }
}

static kern_return_t pvgIOAccelDisplayPipeTransactionEnd(void *displayPipe) {
    if (!originalIOAccelDisplayPipeTransactionEnd)
        originalIOAccelDisplayPipeTransactionEnd =
            (IOAccelDisplayPipeTransactionEndFn)dlsym(
                RTLD_NEXT, "IOAccelDisplayPipeTransactionEnd");
    if (!originalIOAccelDisplayPipeTransactionEnd)
        return KERN_FAILURE;

    if (!ioAccelDisplayPipeTransactionSetTimestamp)
        ioAccelDisplayPipeTransactionSetTimestamp =
            (IOAccelDisplayPipeTransactionSetTimestampFn)dlsym(
                RTLD_DEFAULT,
                "IOAccelDisplayPipeTransactionSetTimestamp");
    if (displayPipe && ioAccelDisplayPipeTransactionSetTimestamp) {
        const uint64_t transactionFlags =
            *(const uint64_t *)((const uint8_t *)displayPipe + 0x40);
        if ((transactionFlags & UINT64_C(0x100)) == 0) {
            // Catalina CoreDisplay already passes the scheduled absolute
            // presentation time to IOPresentmentEnd. Its old IOPresentment
            // implementation does not forward that value to the display
            // pipe, whereas Big Sur's does. Preserve the exact outer value;
            // a new timestamp generated here breaks scheduler/scanout
            // correlation even though submission and completion still match.
            (void)ioAccelDisplayPipeTransactionSetTimestamp(
                displayPipe, pvgPresentationTimestamp
                    ? pvgPresentationTimestamp : mach_absolute_time());
        }
    }

#if PVG_TRACE_RESOURCES
    uint32_t traceSequence = __sync_add_and_fetch(&pvgResourceTraceCount, 1);
    if (displayPipe)
        pvgTraceResource("pipe-before", traceSequence, displayPipe, nil, 8,
                         (const uint8_t *)displayPipe + 0x40, 0x108);
#endif
    kern_return_t result = originalIOAccelDisplayPipeTransactionEnd(displayPipe);
#if PVG_TRACE_RESOURCES
    if (displayPipe)
        pvgTraceResource("pipe-after", traceSequence,
                         (void *)(uintptr_t)(uint32_t)result, nil, 8,
                         (const uint8_t *)displayPipe + 0x40, 0x108);
#endif
    return result;
}

static kern_return_t pvgIOPresentmentEnd(void *presentment, void *transaction,
                                         uint64_t presentationTimestamp,
                                         bool synchronous) {
    if (!originalIOPresentmentEnd)
        originalIOPresentmentEnd = (IOPresentmentEndFn)dlsym(
            RTLD_DEFAULT, "IOPresentmentEnd");
    if (!originalIOPresentmentEnd)
        return KERN_FAILURE;

    const uint64_t previousTimestamp = pvgPresentationTimestamp;
    pvgPresentationTimestamp = presentationTimestamp;
    kern_return_t result = originalIOPresentmentEnd(
        presentment, transaction, presentationTimestamp, synchronous);
    pvgPresentationTimestamp = previousTimestamp;
    return result;
}

#if PVG_FORCE_DISPLAY_SURFACE_SYNC
static kern_return_t pvgCoreDisplayPresentDisplaySurface(
    void *display, void *displaySurface, uint32_t syncType) {
    static CoreDisplaySurfaceGetIOSurfaceFn getIOSurface;
    if (!getIOSurface)
        getIOSurface = (CoreDisplaySurfaceGetIOSurfaceFn)dlsym(
            RTLD_DEFAULT, "CoreDisplay_DisplaySurface_GetIOSurface");

    IOSurfaceRef surface = getIOSurface ? getIOSurface(displaySurface) : NULL;
    if (surface && IOSurfaceLock(surface, kIOSurfaceLockReadOnly, NULL) ==
            kIOReturnSuccess)
        (void)IOSurfaceUnlock(surface, kIOSurfaceLockReadOnly, NULL);

    return originalCoreDisplayPresentDisplaySurface
        ? originalCoreDisplayPresentDisplaySurface(
              display, displaySurface, syncType)
        : KERN_FAILURE;
}
#endif

static void *pvgIOAccelResourceCreate(void *sharedRef, void *args,
                                      uint32_t argsSize) {
    if (!originalIOAccelResourceCreate)
        originalIOAccelResourceCreate = (IOAccelResourceCreateFn)dlsym(
            RTLD_NEXT, "IOAccelResourceCreate");
    if (!originalIOAccelResourceCreate)
        return NULL;

#if PVG_TRACE_RESOURCES
    uint32_t traceSequence = __sync_add_and_fetch(&pvgResourceTraceCount, 1);
    pvgTraceResource("create-before", traceSequence, sharedRef, nil, 0, args,
                     argsSize);
#endif
    void *translatedArgs = NULL;
    void *kernelArgs = args;
    // Resource descriptors, not texture pixels. Keep common descriptors on
    // the user stack; retain a bounded-lifetime heap fallback for larger ones.
    uint64_t localArgs[128];
    if (pvgResourceABITranslationDepth && args && argsSize >= 0x68) {
        if (argsSize <= sizeof(localArgs)) kernelArgs = localArgs;
        else {
            translatedArgs = malloc(argsSize);
            if (!translatedArgs) return NULL; // Never submit the wrong ABI.
            kernelArgs = translatedArgs;
        }
        memcpy(kernelArgs, args, argsSize);
        pvgTranslateResourceArgsToBigSur(kernelArgs, argsSize);
    }
#if PVG_TRACE_RESOURCES
    if (kernelArgs != args)
        pvgTraceResource("create-translated", traceSequence, sharedRef, nil,
                         0, kernelArgs, argsSize);
#endif
    void *result = originalIOAccelResourceCreate(sharedRef, kernelArgs,
                                                 argsSize);
    free(translatedArgs);
#if PVG_TRACE_RESOURCES
    pvgTraceResource("create-after", traceSequence, result, nil, 0, args,
                     argsSize);
#endif
    return result;
}

static kern_return_t pvgIOConnectCallStructMethod(
    mach_port_t connection, uint32_t selector, const void *input,
    size_t inputSize, void *output, size_t *outputSize) {
    if (!originalIOConnectCallStructMethod)
        originalIOConnectCallStructMethod =
            (IOConnectCallStructMethodFn)dlsym(RTLD_NEXT,
                                               "IOConnectCallStructMethod");
    if (!originalIOConnectCallStructMethod)
        return KERN_FAILURE;

#if PVG_TRACE_RESOURCES
    uint32_t traceSequence = 0;
    if (input && inputSize >= 0x60 && inputSize <= 0x200) {
        traceSequence = __sync_add_and_fetch(&pvgResourceTraceCount, 1);
        pvgTraceResource("iokit-before", traceSequence,
                         (void *)(uintptr_t)connection, nil, selector, input,
                         (unsigned int)inputSize);
    }
#endif
    kern_return_t result = originalIOConnectCallStructMethod(
        connection, selector, input, inputSize, output, outputSize);
#if PVG_TRACE_RESOURCES
    if (traceSequence)
        pvgTraceResource("iokit-after", traceSequence,
                         (void *)(uintptr_t)(uint32_t)result, nil, selector,
                         input, (unsigned int)inputSize);
#endif
    return result;
}

static void pvgRebindIOAccelCreateInImage(const struct mach_header *header,
                                          intptr_t slide) {
    if (!header || header->magic != MH_MAGIC_64)
        return;

    const struct mach_header_64 *header64 =
        (const struct mach_header_64 *)header;
    const struct segment_command_64 *linkedit = NULL;
    const struct symtab_command *symtabCommand = NULL;
    const struct dysymtab_command *dysymtabCommand = NULL;
    const uint8_t *commandBytes = (const uint8_t *)(header64 + 1);
    const struct load_command *command =
        (const struct load_command *)commandBytes;
    for (uint32_t index = 0; index < header64->ncmds; ++index) {
        if (command->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *segment =
                (const struct segment_command_64 *)command;
            if (strcmp(segment->segname, SEG_LINKEDIT) == 0)
                linkedit = segment;
        } else if (command->cmd == LC_SYMTAB) {
            symtabCommand = (const struct symtab_command *)command;
        } else if (command->cmd == LC_DYSYMTAB) {
            dysymtabCommand = (const struct dysymtab_command *)command;
        }
        command = (const struct load_command *)((const uint8_t *)command +
                                                 command->cmdsize);
    }
    if (!linkedit || !symtabCommand || !dysymtabCommand)
        return;

    uintptr_t linkeditBase =
        (uintptr_t)slide + linkedit->vmaddr - linkedit->fileoff;
    const struct nlist_64 *symbols = (const struct nlist_64 *)(
        linkeditBase + symtabCommand->symoff);
    const char *strings =
        (const char *)(linkeditBase + symtabCommand->stroff);
    const uint32_t *indirectSymbols = (const uint32_t *)(
        linkeditBase + dysymtabCommand->indirectsymoff);

    command = (const struct load_command *)commandBytes;
    for (uint32_t commandIndex = 0; commandIndex < header64->ncmds;
         ++commandIndex) {
        if (command->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *segment =
                (const struct segment_command_64 *)command;
            const struct section_64 *section =
                (const struct section_64 *)(segment + 1);
            for (uint32_t sectionIndex = 0; sectionIndex < segment->nsects;
                 ++sectionIndex, ++section) {
                uint32_t sectionType = section->flags & SECTION_TYPE;
                if (sectionType != S_LAZY_SYMBOL_POINTERS &&
                    sectionType != S_NON_LAZY_SYMBOL_POINTERS)
                    continue;
                void **bindings = (void **)((uintptr_t)slide + section->addr);
                size_t bindingCount = section->size / sizeof(void *);
                for (size_t bindingIndex = 0; bindingIndex < bindingCount;
                     ++bindingIndex) {
                    uint32_t symbolIndex = indirectSymbols[
                        section->reserved1 + bindingIndex];
                    if (symbolIndex == INDIRECT_SYMBOL_ABS ||
                        symbolIndex == INDIRECT_SYMBOL_LOCAL ||
                        symbolIndex == (INDIRECT_SYMBOL_LOCAL |
                                        INDIRECT_SYMBOL_ABS))
                        continue;
                    const char *symbolName =
                        strings + symbols[symbolIndex].n_un.n_strx;
                    BOOL isResourceCreate =
                        strcmp(symbolName, "_IOAccelResourceCreate") == 0;
                    BOOL isStructMethod =
#if PVG_TRACE_RESOURCES
                        strcmp(symbolName, "_IOConnectCallStructMethod") == 0;
#else
                        NO;
#endif
                    BOOL isDisplayPipeEnd = strcmp(
                        symbolName,
                        "_IOAccelDisplayPipeTransactionEnd") == 0;
                    BOOL isPresentmentEnd =
                        strcmp(symbolName, "_IOPresentmentEnd") == 0;
                    BOOL isCoreDisplayPresent =
#if PVG_FORCE_DISPLAY_SURFACE_SYNC
                        strcmp(symbolName,
                               "_CoreDisplay_Display_PresentDisplaySurface") ==
                        0;
#else
                        NO;
#endif
                    if (!isResourceCreate && !isStructMethod &&
                        !isDisplayPipeEnd && !isPresentmentEnd &&
                        !isCoreDisplayPresent)
                        continue;
                    if (isResourceCreate && !originalIOAccelResourceCreate)
                        originalIOAccelResourceCreate = (IOAccelResourceCreateFn)
                            bindings[bindingIndex];
                    if (isStructMethod && !originalIOConnectCallStructMethod)
                        originalIOConnectCallStructMethod =
                            (IOConnectCallStructMethodFn)bindings[bindingIndex];
                    if (isDisplayPipeEnd &&
                        !originalIOAccelDisplayPipeTransactionEnd)
                        originalIOAccelDisplayPipeTransactionEnd =
                            (IOAccelDisplayPipeTransactionEndFn)
                                bindings[bindingIndex];
                    if (isPresentmentEnd && !originalIOPresentmentEnd)
                        originalIOPresentmentEnd = (IOPresentmentEndFn)
                            bindings[bindingIndex];
                    if (isCoreDisplayPresent &&
                        !originalCoreDisplayPresentDisplaySurface)
                        originalCoreDisplayPresentDisplaySurface =
                            (CoreDisplayPresentDisplaySurfaceFn)
                                bindings[bindingIndex];
                    mach_vm_address_t page =
                        (mach_vm_address_t)(uintptr_t)&bindings[bindingIndex] &
                        ~((mach_vm_address_t)vm_page_size - 1);
                    if (mach_vm_protect(mach_task_self(), page, vm_page_size,
                                        FALSE,
                                        VM_PROT_READ | VM_PROT_WRITE |
                                            VM_PROT_COPY) == KERN_SUCCESS)
                        bindings[bindingIndex] = isResourceCreate
                            ? (void *)pvgIOAccelResourceCreate
                            : isStructMethod
                                ? (void *)pvgIOConnectCallStructMethod
                                : isDisplayPipeEnd
                                    ? (void *)pvgIOAccelDisplayPipeTransactionEnd
                                    : isPresentmentEnd
                                        ? (void *)pvgIOPresentmentEnd
#if PVG_FORCE_DISPLAY_SURFACE_SYNC
                                        : (void *)pvgCoreDisplayPresentDisplaySurface;
#else
                                        : bindings[bindingIndex];
#endif
                }
            }
        }
        command = (const struct load_command *)((const uint8_t *)command +
                                                 command->cmdsize);
    }
}

static void pvgInstallIOAccelCreateTrace(void) {
    originalIOAccelResourceCreate =
        (IOAccelResourceCreateFn)dlsym(RTLD_DEFAULT,
                                       "IOAccelResourceCreate");
    originalIOConnectCallStructMethod =
#if PVG_TRACE_RESOURCES
        (IOConnectCallStructMethodFn)dlsym(RTLD_DEFAULT,
                                           "IOConnectCallStructMethod");
#else
        NULL;
#endif
    originalIOAccelDisplayPipeTransactionEnd =
        (IOAccelDisplayPipeTransactionEndFn)dlsym(
            RTLD_DEFAULT, "IOAccelDisplayPipeTransactionEnd");
    originalIOPresentmentEnd =
        (IOPresentmentEndFn)dlsym(RTLD_DEFAULT, "IOPresentmentEnd");
    // dyld invokes this callback for every existing image as well as images
    // loaded later. The resource-create binding is the ABI boundary: Catalina
    // first finishes allocating its old-layout descriptor, then this adapter
    // sends a translated copy to the Big Sur kernel stack.
    _dyld_register_func_for_add_image(pvgRebindIOAccelCreateInImage);
}

typedef void *LLVMMemoryBufferRef;
typedef void *LLVMModuleRef;
typedef void *LLVMContextRef;

typedef id (*MTLNewComputePipelineFn)(id, SEL, id, NSError **);
static MTLNewComputePipelineFn originalNewComputePipeline;
typedef id (*MTLNewComputePipelineOptionsFn)(
    id, SEL, id, MTLPipelineOption, MTLComputePipelineReflection **,
    NSError **);
static MTLNewComputePipelineOptionsFn originalNewComputePipelineOptions;
typedef void (*MTLNewComputePipelineAsyncFn)(
    id, SEL, id, MTLNewComputePipelineStateCompletionHandler);
static MTLNewComputePipelineAsyncFn originalNewComputePipelineAsync;
typedef void (*MTLNewComputePipelineOptionsAsyncFn)(
    id, SEL, id, MTLPipelineOption,
    MTLNewComputePipelineStateWithReflectionCompletionHandler);
static MTLNewComputePipelineOptionsAsyncFn
    originalNewComputePipelineOptionsAsync;
typedef id (*MTLNewComputeDescriptorOptionsFn)(
    id, SEL, MTLComputePipelineDescriptor *, MTLPipelineOption,
    MTLComputePipelineReflection **, NSError **);
static MTLNewComputeDescriptorOptionsFn originalNewComputeDescriptorOptions;
typedef void (*MTLNewComputeDescriptorOptionsAsyncFn)(
    id, SEL, MTLComputePipelineDescriptor *, MTLPipelineOption,
    MTLNewComputePipelineStateWithReflectionCompletionHandler);
static MTLNewComputeDescriptorOptionsAsyncFn
    originalNewComputeDescriptorOptionsAsync;

typedef id (*MTLNewRenderPipelineFn)(id, SEL, MTLRenderPipelineDescriptor *,
                                     NSError **);
static MTLNewRenderPipelineFn originalNewRenderPipeline;
typedef id (*MTLNewRenderPipelineOptionsFn)(
    id, SEL, MTLRenderPipelineDescriptor *, MTLPipelineOption,
    MTLRenderPipelineReflection **, NSError **);
static MTLNewRenderPipelineOptionsFn originalNewRenderPipelineOptions;
typedef void (*MTLNewRenderPipelineAsyncFn)(
    id, SEL, MTLRenderPipelineDescriptor *,
    MTLNewRenderPipelineStateCompletionHandler);
static MTLNewRenderPipelineAsyncFn originalNewRenderPipelineAsync;
typedef void (*MTLNewRenderPipelineOptionsAsyncFn)(
    id, SEL, MTLRenderPipelineDescriptor *, MTLPipelineOption,
    MTLNewRenderPipelineStateWithReflectionCompletionHandler);
static MTLNewRenderPipelineOptionsAsyncFn originalNewRenderPipelineOptionsAsync;

typedef id (*PVGTextureInitDeviceFn)(id, SEL, id, MTLTextureDescriptor *);
typedef id (*PVGTextureInitBufferFn)(id, SEL, id, MTLTextureDescriptor *,
                                     NSUInteger, NSUInteger);
typedef id (*PVGTextureInitIOSurfaceFn)(id, SEL, id, MTLTextureDescriptor *,
                                        void *, NSUInteger, NSUInteger);
static PVGTextureInitDeviceFn originalTextureInitDevice;
static PVGTextureInitBufferFn originalTextureInitBuffer;
static PVGTextureInitBufferFn originalTextureInitTiledBuffer;
static PVGTextureInitIOSurfaceFn originalTextureInitIOSurface;
static ptrdiff_t pvgTextureUsageOffset = -1;

// The donor initializers do not populate Catalina's _usage ivar. Repair the
// guest API metadata as well as the wire descriptor: Core Image tests
// texture.usage & ShaderWrite before assembling decoded image tiles and
// otherwise silently commits an empty command buffer for every tile.
static void pvgRestoreTextureUsage(id texture, MTLTextureDescriptor *descriptor) {
    if (!texture || !descriptor) return;
    if (pvgTextureUsageOffset >= 0)
        *(NSUInteger *)((uint8_t *)(__bridge void *)texture + pvgTextureUsageOffset) = descriptor.usage;
}

static void pvgSetTextureDescriptor(MTLTextureDescriptor *descriptor,
                                    NSUInteger *savedUsage,
                                    MTLTextureType *savedType,
                                    MTLTextureDescriptor **savedDescriptor,
                                    BOOL *savedActive) {
    *savedUsage = pvgCurrentTextureUsage;
    *savedType = pvgCurrentTextureType;
    *savedDescriptor = pvgCurrentTextureDescriptor;
    *savedActive = pvgHasTextureDescriptor;
    pvgCurrentTextureUsage = descriptor ? descriptor.usage : 0;
    pvgCurrentTextureType = descriptor ? descriptor.textureType : 0;
    pvgCurrentTextureDescriptor = descriptor;
    pvgHasTextureDescriptor = descriptor != nil;
}

static void pvgRestoreTextureDescriptor(NSUInteger savedUsage,
                                        MTLTextureType savedType,
                                        MTLTextureDescriptor *savedDescriptor,
                                        BOOL savedActive) {
    pvgCurrentTextureUsage = savedUsage;
    pvgCurrentTextureType = savedType;
    pvgCurrentTextureDescriptor = savedDescriptor;
    pvgHasTextureDescriptor = savedActive;
}

static id pvgTextureInitDevice(id self, SEL selector, id device,
                               MTLTextureDescriptor *descriptor) {
    NSUInteger savedUsage;
    MTLTextureType savedType;
    MTLTextureDescriptor *savedDescriptor;
    BOOL savedActive;
    pvgSetTextureDescriptor(descriptor, &savedUsage, &savedType,
                            &savedDescriptor, &savedActive);
    id result = originalTextureInitDevice(self, selector, device, descriptor);
    pvgRestoreTextureUsage(result, descriptor);
    pvgRestoreTextureDescriptor(savedUsage, savedType, savedDescriptor,
                                savedActive);
    return result;
}

static id pvgTextureInitBuffer(id self, SEL selector, id buffer,
                               MTLTextureDescriptor *descriptor,
                               NSUInteger offset, NSUInteger bytesPerRow) {
    NSUInteger savedUsage;
    MTLTextureType savedType;
    MTLTextureDescriptor *savedDescriptor;
    BOOL savedActive;
    pvgSetTextureDescriptor(descriptor, &savedUsage, &savedType,
                            &savedDescriptor, &savedActive);
    id result = originalTextureInitBuffer(self, selector, buffer, descriptor,
                                          offset, bytesPerRow);
    pvgRestoreTextureUsage(result, descriptor);
    pvgRestoreTextureDescriptor(savedUsage, savedType, savedDescriptor,
                                savedActive);
    return result;
}

static id pvgTextureInitTiledBuffer(id self, SEL selector, id buffer,
                                    MTLTextureDescriptor *descriptor,
                                    NSUInteger offset,
                                    NSUInteger bytesPerRow) {
    NSUInteger savedUsage;
    MTLTextureType savedType;
    MTLTextureDescriptor *savedDescriptor;
    BOOL savedActive;
    pvgSetTextureDescriptor(descriptor, &savedUsage, &savedType,
                            &savedDescriptor, &savedActive);
    id result = originalTextureInitTiledBuffer(
        self, selector, buffer, descriptor, offset, bytesPerRow);
    pvgRestoreTextureUsage(result, descriptor);
    pvgRestoreTextureDescriptor(savedUsage, savedType, savedDescriptor,
                                savedActive);
    return result;
}

static id pvgTextureInitIOSurface(id self, SEL selector, id device,
                                  MTLTextureDescriptor *descriptor,
                                  void *ioSurface, NSUInteger plane,
                                  NSUInteger field) {
#if PVG_TRACE_RESOURCES
    if (getenv("PVG_TRACE_SURFACE_PIXELS") && ioSurface) {
        IOSurfaceRef s = (IOSurfaceRef)ioSurface;
        IOReturn lock = IOSurfaceLock(s, kIOSurfaceLockReadOnly, NULL);
        if (!lock) {
            const unsigned char *p = IOSurfaceGetBaseAddress(s);
            size_t n = IOSurfaceGetAllocSize(s);
            unsigned long long sum = 0;
            for (size_t i = 0; p && i < n; i += 97) sum += p[i];
            fprintf(stderr, "PVG surface id=%u size=%zux%zu alloc=%zu format=%lx texture=%lux%lu pixel=%lu options=%lx sampledSum=%llu\n",
                IOSurfaceGetID(s), IOSurfaceGetWidth(s), IOSurfaceGetHeight(s), n,
                (unsigned long)IOSurfaceGetPixelFormat(s),
                (unsigned long)descriptor.width,(unsigned long)descriptor.height,
                (unsigned long)descriptor.pixelFormat,(unsigned long)descriptor.resourceOptions,sum);
            IOSurfaceUnlock(s, kIOSurfaceLockReadOnly, NULL);
        }
    }
#endif
    NSUInteger savedUsage;
    MTLTextureType savedType;
    MTLTextureDescriptor *savedDescriptor;
    NSUInteger savedPlane = pvgCurrentTexturePlane;
    BOOL savedActive;
    pvgSetTextureDescriptor(descriptor, &savedUsage, &savedType,
                            &savedDescriptor, &savedActive);
    pvgCurrentTexturePlane = plane;
    id result = originalTextureInitIOSurface(
        self, selector, device, descriptor, ioSurface, plane, field);
    pvgRestoreTextureUsage(result, descriptor);
    pvgCurrentTexturePlane = savedPlane;
    pvgRestoreTextureDescriptor(savedUsage, savedType, savedDescriptor,
                                savedActive);
    return result;
}

static void pvgInstallTextureUsageHooks(void) {
    Class textureClass = objc_getClass("AppleParavirtTexture");
    if (!textureClass)
        return;
    Ivar usage = class_getInstanceVariable(objc_getClass("MTLIOAccelTexture"), "_usage");
    if (usage && strcmp(ivar_getTypeEncoding(usage), @encode(NSUInteger)) == 0)
        pvgTextureUsageOffset = ivar_getOffset(usage);

#define PVG_INSTALL_TEXTURE_HOOK(selectorName, original, replacement) do { \
    SEL hookSelector = sel_registerName(selectorName); \
    Method hookMethod = class_getInstanceMethod(textureClass, hookSelector); \
    if (hookMethod && method_getImplementation(hookMethod) != \
                          (IMP)(replacement)) { \
        (original) = (__typeof__(original))method_getImplementation(hookMethod); \
        method_setImplementation(hookMethod, (IMP)(replacement)); \
    } \
} while (0)
    PVG_INSTALL_TEXTURE_HOOK("initWithDevice:descriptor:",
                             originalTextureInitDevice,
                             pvgTextureInitDevice);
    PVG_INSTALL_TEXTURE_HOOK("initWithBuffer:descriptor:offset:bytesPerRow:",
                             originalTextureInitBuffer,
                             pvgTextureInitBuffer);
    PVG_INSTALL_TEXTURE_HOOK(
        "initTiledWithBuffer:descriptor:offset:bytesPerRow:",
        originalTextureInitTiledBuffer, pvgTextureInitTiledBuffer);
    PVG_INSTALL_TEXTURE_HOOK(
        "initWithDevice:descriptor:iosurface:plane:field:",
        originalTextureInitIOSurface, pvgTextureInitIOSurface);
#undef PVG_INSTALL_TEXTURE_HOOK
}

// AppleParavirtGPUMetal is the loader of this framework. Its Objective-C
// classes are not registered yet when a dependency constructor runs, so that
// constructor cannot reliably find AppleParavirtDevice. A category +load is
// deferred until the Objective-C runtime registers that class. The class
// symbol resolves from the loading plugin at runtime.
@interface AppleParavirtDevice : NSObject
@end

@interface AppleParavirtDevice (PVGMetalCompatLoad)
@end

#if PVG_TRACE_COMMANDS
extern void PVGInstallCommandTrace(void);
#endif
#if PVG_SKIP_WINDOWSERVER_MIPMAPS
extern void PVGInstallWindowServerMipmapProbe(void);
#endif

static char executableKey;
static char nativeAIRCheckedKey;

#if PVG_REPAIR_LEGACY_AIR
static BOOL pvgLegacyLibraryHasSource(const void *library) {
    const void *const *vtable = *(const void *const **)library;
    const void *const *typeinfo = vtable ? vtable[-1] : NULL;
    const char *typeName = typeinfo ? typeinfo[1] : NULL;
    return typeName && !strcmp(typeName, "24MTLLibraryDataWithSource");
}
#endif

static BOOL pvgColdFunction(id function) {
    if (!function || objc_getAssociatedObject(function,&executableKey) ||
        objc_getAssociatedObject(function,&nativeAIRCheckedKey)) return NO;
    SEL fd=sel_registerName("functionData"), ld=sel_registerName("libraryData");
    if (![function respondsToSelector:fd] || ![function respondsToSelector:ld])return NO;
    void *(*pointer)(id,SEL)=(void *)objc_msgSend;
    const uint8_t *data=pointer(function,fd);
    if(!data)return NO;
    uint16_t major=*(const uint16_t *)(data+0x28), minor=*(const uint16_t *)(data+0x2a);
    if ((major<2 || (major==2 && minor<=2)) &&
        (!PVG_REPAIR_LEGACY_AIR || [function functionType] != MTLFunctionTypeKernel)) return NO;
    const uint32_t *library=pointer(function,ld);
    if (!library || *library == UINT32_C(0x424c544d)) return NO;
#if PVG_REPAIR_LEGACY_AIR
    // Match the synchronous guard before scheduling a worker or copying a
    // descriptor. Archive-backed native kernels never require our repair.
    if ((major < 2 || (major == 2 && minor <= 2)) &&
        !pvgLegacyLibraryHasSource(library)) return NO;
#endif
    return YES;
}

static void pvgPrepareAsyncPipeline(BOOL cold, dispatch_block_t prepare) {
    if(cold) dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0), ^{
        @autoreleasepool { prepare(); }
    });
    else prepare();
}

static id pvgExecutableFunction(id device, id function) {
    if (!device || !function ||
        strcmp(object_getClassName(device), "AppleParavirtDevice") != 0)
        return function;
    // Functions are immutable and belong to one device. Bound conversion
    // lifetime to that function, not a global/unbounded shader cache.
    id cached = objc_getAssociatedObject(function, &executableKey);
    if (cached) return cached;
    if (objc_getAssociatedObject(function, &nativeAIRCheckedKey)) return function;
    if (pvgCompilerTraceEnabled()) {
    char entryTrace[160];
    snprintf(entryTrace, sizeof(entryTrace), "entered name=%s type=%lu",
             [function name].UTF8String ?: "(null)",
             (unsigned long)[function functionType]);
    pvgCompilerTrace(entryTrace);
    }

    SEL functionDataSelector = sel_registerName("functionData");
    SEL libraryDataSelector = sel_registerName("libraryData");
    if (![function respondsToSelector:functionDataSelector] ||
        ![function respondsToSelector:libraryDataSelector]) {
        pvgCompilerTrace("missing functionData/libraryData selector");
        return function;
    }

    void *(*sendPointer)(id, SEL) = (void *(*)(id, SEL))objc_msgSend;
    uint8_t *functionData = sendPointer(function, functionDataSelector);
    if (!functionData) {
        pvgCompilerTrace("null functionData");
        return function;
    }

    uint16_t airMajor = *(uint16_t *)(functionData + 0x28);
    uint16_t airMinor = *(uint16_t *)(functionData + 0x2a);
    BOOL legacyAIR = airMajor < 2 || (airMajor == 2 && airMinor <= 2);
    // The proven legacy metadata defect is in Core Image compute kernels.
    // Existing executable render functions have another private library-data
    // representation and must retain their original fast path.
    if (legacyAIR && (!PVG_REPAIR_LEGACY_AIR ||
                      [function functionType] != MTLFunctionTypeKernel)) {
        pvgCompilerTrace("already executable AIR");
        return function;
    }

    uint8_t *libraryData = sendPointer(function, libraryDataSelector);
    if (!libraryData) {
        pvgCompilerTrace("null libraryData");
        return function;
    }

    // For an executable MTLLibrary, AppleParavirt's private `libraryData`
    // accessor returns the mapped metallib bytes directly.  Only source/AIR
    // libraries return the Catalina Metal library-data object whose mapping
    // lives at +0x48/+0x50.  Do not interpret the leading MTLB header as an
    // object pointer; executable functions are already suitable for the PVG
    // plugin and need no downlevel/recompile pass.
    uint32_t libraryMagic = 0;
    memcpy(&libraryMagic, libraryData, sizeof(libraryMagic));
    if (libraryMagic == UINT32_C(0x424c544d)) { // "MTLB"
        pvgCompilerTrace("already MTLB");
        return function;
    }
#if PVG_REPAIR_LEGACY_AIR
    if (legacyAIR) {
        // Native Mojave RTTI confirms two distinct library-data layouts.
        // Only WithSource owns mapped bytes/length at +0x48/+0x50.
        // WithArchive's +0x50 is a pointer, NOT a byte count (MPS uses it).
        if (!pvgLegacyLibraryHasSource(libraryData)) {
            pvgCompilerTrace("legacy archive AIR: preserve native path");
            objc_setAssociatedObject(function, &nativeAIRCheckedKey, @YES,
                                     OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            return function;
        }
    }
#endif

    // Single-flight only for genuinely cold conversion. Independent functions
    // still compile concurrently; cached/native functions never take this lock.
    @synchronized(function) {
    id completed = objc_getAssociatedObject(function,&executableKey);
    if(completed) return completed;
    if (objc_getAssociatedObject(function, &nativeAIRCheckedKey)) return function;

    const uint8_t *mappedBytes = *(const uint8_t **)(libraryData + 0x48);
    size_t mappedLength = *(size_t *)(libraryData + 0x50);
    uint64_t functionBitcodeOffset = 0;
    memcpy(&functionBitcodeOffset, functionData, sizeof(functionBitcodeOffset));
    // MTLFunctionData.bitCodeOffset addresses a length-prefixed AIR record in
    // the shared library mapping. Source libraries contain one such record per
    // function; scanning for the first BC magic therefore always selected the
    // vertex module when compiling a fragment pipeline.
    if (!mappedBytes || functionBitcodeOffset > mappedLength ||
        mappedLength - (size_t)functionBitcodeOffset < 24) {
        pvgCompilerTrace("bad function AIR offset");
        return function;
    }

    const uint8_t *record = mappedBytes + (size_t)functionBitcodeOffset;
    uint32_t recordLength = 0;
    uint32_t wrapperMagic = 0;
    uint32_t wrapperBitcodeOffset = 0;
    uint32_t bitcodeLength = 0;
    memcpy(&recordLength, record, 4);
    memcpy(&wrapperMagic, record + 4, 4);
    memcpy(&wrapperBitcodeOffset, record + 12, 4);
    memcpy(&bitcodeLength, record + 16, 4);
    size_t available = mappedLength - (size_t)functionBitcodeOffset;
    // The four-byte Metal library record prefix stores the byte count of the
    // following LLVM wrapper; it does not include itself. LLVM's authoritative
    // SkipBitcodeWrapperHeader check therefore bounds offset+size against
    // recordLength, while mapped-library bounds include the extra prefix.
    if (wrapperMagic != UINT32_C(0x0b17c0de) ||
        recordLength < 20 || (size_t)recordLength + 4 > available ||
        wrapperBitcodeOffset < 20 ||
        wrapperBitcodeOffset > recordLength ||
        bitcodeLength > recordLength - wrapperBitcodeOffset) {
        if (pvgCompilerTraceEnabled()) {
        char badWrapperTrace[512];
        int traceLength = snprintf(
            badWrapperTrace, sizeof(badWrapperTrace),
            "bad AIR wrapper functionOffset=0x%llx available=0x%zx "
            "record=0x%x magic=0x%08x bitcodeOffset=0x%x "
            "bitcodeLength=0x%x bytes=",
            functionBitcodeOffset, available, recordLength, wrapperMagic,
            wrapperBitcodeOffset, bitcodeLength);
        size_t byteCount = available < 48 ? available : 48;
        for (size_t index = 0;
             index < byteCount && traceLength > 0 &&
                 (size_t)traceLength < sizeof(badWrapperTrace) - 4;
             ++index) {
            traceLength += snprintf(badWrapperTrace + traceLength,
                                    sizeof(badWrapperTrace) - traceLength,
                                    "%02x", record[index]);
        }
        pvgCompilerTrace(badWrapperTrace);
        }
        return function;
    }
    size_t bitcodeOffset = (size_t)functionBitcodeOffset + 4 +
                           wrapperBitcodeOffset;
    static const uint8_t bitcodeMagic[] = {'B', 'C', 0xc0, 0xde};
    if (bitcodeLength < sizeof(bitcodeMagic) ||
        memcmp(mappedBytes + bitcodeOffset, bitcodeMagic,
               sizeof(bitcodeMagic)) != 0) {
        pvgCompilerTrace("bad function AIR bitcode");
        return function;
    }
    if (pvgCompilerTraceEnabled()) {
    char offsetTrace[192];
    snprintf(offsetTrace, sizeof(offsetTrace),
             "AIR record offset=0x%llx record=0x%x bitcode=0x%zx+0x%x",
             functionBitcodeOffset, recordLength, bitcodeOffset,
             bitcodeLength);
    pvgCompilerTrace(offsetTrace);
    }

    NSString *cachePath = pvgShaderCachePath(mappedBytes + bitcodeOffset,
        bitcodeLength, [function name], [function functionType]);
    NSData *cachedMetallib = pvgReadShaderCache(cachePath);
    if (cachedMetallib) {
        id cachedFunction = pvgFunctionFromCompiled(device, function, cachedMetallib);
        if (cachedFunction) {
            objc_setAssociatedObject(function, &executableKey, cachedFunction,
                                     OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            pvgCompilerTrace("persistent AIR cache hit");
            return cachedFunction;
        }
        pvgCompilerTrace("persistent AIR cache rejected; compiling");
    }

    static void *llvmImage;
    static void *pluginImage;
    static dispatch_once_t compilerImagesOnce;
    dispatch_once(&compilerImagesOnce, ^{
    llvmImage = dlopen(
        "/System/Library/PrivateFrameworks/GPUCompiler.framework/Versions/"
        "31001/Libraries/libLLVM.dylib", RTLD_NOW | RTLD_LOCAL);
    pluginImage = dlopen(
        "/System/Library/Extensions/AppleParavirtGPUMetal.bundle/Contents/"
        "Resources/libAppleParavirtCompilerPluginReal.dylib",
        RTLD_NOW | RTLD_LOCAL);
    });
    if (!llvmImage || !pluginImage) {
        pvgCompilerTrace("compiler dlopen failure");
        return function;
    }

    LLVMMemoryBufferRef (*createBuffer)(const char *, size_t, const char *) =
        dlsym(llvmImage, "LLVMCreateMemoryBufferWithMemoryRangeCopy");
    LLVMContextRef (*createContext)(void) =
        dlsym(llvmImage, "LLVMContextCreate");
    void (*disposeContext)(LLVMContextRef) =
        dlsym(llvmImage, "LLVMContextDispose");
    int (*parseBitcode)(LLVMContextRef, LLVMMemoryBufferRef, LLVMModuleRef *) =
        dlsym(llvmImage, "LLVMParseBitcodeInContext2");
    void (*setTarget)(LLVMModuleRef, const char *) =
        dlsym(llvmImage, "LLVMSetTarget");
    void (*disposeModule)(LLVMModuleRef) =
        dlsym(llvmImage, "LLVMDisposeModule");
    void (*disposeBuffer)(LLVMMemoryBufferRef) =
        dlsym(llvmImage, "LLVMDisposeMemoryBuffer");
    void *(*getNamedMetadata)(LLVMModuleRef, const void *) =
        dlsym(llvmImage,
              "_ZNK4llvm6Module16getNamedMetadataERKNS_5TwineE");
    void (*eraseNamedMetadata)(LLVMModuleRef, void *) =
        dlsym(llvmImage,
              "_ZN4llvm6Module18eraseNamedMetadataEPNS_11NamedMDNodeE");
    unsigned (*getNamedMetadataNumOperands)(LLVMModuleRef, const char *) =
        dlsym(llvmImage, "LLVMGetNamedMetadataNumOperands");
    void (*getNamedMetadataOperands)(LLVMModuleRef, const char *, void **) =
        dlsym(llvmImage, "LLVMGetNamedMetadataOperands");
    void *(*getOperand)(void *, unsigned) =
        dlsym(llvmImage, "LLVMGetOperand");
    void (*deleteFunction)(void *) =
        dlsym(llvmImage, "LLVMDeleteFunction");
    char *(*printModule)(LLVMModuleRef) =
        dlsym(llvmImage, "LLVMPrintModuleToString");
    void (*disposeMessage)(char *) =
        dlsym(llvmImage, "LLVMDisposeMessage");
    void *(*createCompiler)(void) = dlsym(pluginImage, "MTLCompilerCreate");
    int (*buildRequest)(void *, const void *, size_t, unsigned, LLVMModuleRef,
                        const void **, size_t *, void **, size_t *, void **,
                        size_t *, const char **) =
        dlsym(pluginImage, "MTLCompilerBuildRequestWithOptions");
    void (*releaseReply)(void *) =
        dlsym(pluginImage, "MTLCompilerReleaseReply");
    void (*deleteCompiler)(void *) =
        dlsym(pluginImage, "MTLCompilerDelete");
    if (!createBuffer || !createContext || !disposeContext || !parseBitcode ||
        !setTarget || !disposeModule || !disposeBuffer || !getNamedMetadata ||
        !getNamedMetadataNumOperands || !getNamedMetadataOperands ||
        !getOperand || !deleteFunction ||
        !createCompiler || !buildRequest || !releaseReply || !deleteCompiler) {
        pvgCompilerTrace("compiler dlsym failure");
        return function;
    }

    LLVMContextRef context = createContext();
    LLVMMemoryBufferRef input = createBuffer(
        (const char *)mappedBytes + bitcodeOffset, bitcodeLength, "PVG AIR");
    LLVMModuleRef module = NULL;
    if (!context || !input || parseBitcode(context, input, &module) != 0 ||
        !module) {
        pvgCompilerTrace("AIR parse failure");
        if (input)
            disposeBuffer(input);
        if (context)
            disposeContext(context);
        return function;
    }

#if PVG_REPAIR_LEGACY_AIR
    int repairedBuffers = PVGCanonicalizeAIRBuffers(llvmImage, module, context);
    if (repairedBuffers < 0 || (legacyAIR && repairedBuffers == 0)) {
        disposeModule(module);
        disposeBuffer(input);
        disposeContext(context);
        if (repairedBuffers == 0)
            objc_setAssociatedObject(function, &nativeAIRCheckedKey, @YES,
                                     OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        pvgCompilerTrace(repairedBuffers < 0 ? "AIR metadata repair failed" :
                         "native AIR metadata checked: unchanged");
        return function;
    }
    if (repairedBuffers) pvgCompilerTrace("canonicalized AIR buffer metadata");
#endif

    // AppleParavirtCompiler emits one MetalLib function: the first entry-point
    // named metadata it finds, in vertex/fragment/kernel order. Isolate the
    // requested stage by removing the other entry-point nodes. The Twine ABI
    // here is not inferred: Apple LLVM's checked-out 20210107 source defines
    // two pointer-sized children followed by CStringKind=3 and EmptyKind=1,
    // and this exact plugin constructs the same 0x18-byte value at 0x76662.
    typedef struct {
        const char *lhs;
        const void *rhs;
        uint8_t lhsKind;
        uint8_t rhsKind;
        uint8_t padding[6];
    } PVGLLVMTwine;
    MTLFunctionType functionType = [function functionType];
    const char *metadataNames[] = {"air.vertex", "air.fragment", "air.kernel"};
    const MTLFunctionType metadataTypes[] = {
        MTLFunctionTypeVertex, MTLFunctionTypeFragment, MTLFunctionTypeKernel
    };
    for (size_t index = 0; index < 3; ++index) {
        if (metadataTypes[index] == functionType)
            continue;
        unsigned operandCount =
            getNamedMetadataNumOperands(module, metadataNames[index]);
        void **metadataOperands = operandCount
            ? calloc(operandCount, sizeof(*metadataOperands)) : NULL;
        void **entryFunctions = operandCount
            ? calloc(operandCount, sizeof(*entryFunctions)) : NULL;
        if (operandCount && (!metadataOperands || !entryFunctions)) {
            free(entryFunctions);
            free(metadataOperands);
            disposeModule(module);
            disposeBuffer(input);
            disposeContext(context);
            return function;
        }
        if (metadataOperands && entryFunctions) {
            getNamedMetadataOperands(module, metadataNames[index],
                                     metadataOperands);
            for (unsigned operand = 0; operand < operandCount; ++operand)
                entryFunctions[operand] = getOperand(metadataOperands[operand], 0);
        }
        PVGLLVMTwine name = {
            metadataNames[index], NULL, 3, 1, {0, 0, 0, 0, 0, 0}
        };
        void *node = getNamedMetadata(module, &name);
        if (node)
            eraseNamedMetadata(module, node);
        if (entryFunctions) {
            for (unsigned operand = 0; operand < operandCount; ++operand) {
                if (entryFunctions[operand])
                    deleteFunction(entryFunctions[operand]);
            }
        }
        free(entryFunctions);
        free(metadataOperands);
    }
    const char *dumpIRDirectory = getenv("PVG_DUMP_ISOLATED_LL_DIR");
    if (dumpIRDirectory && *dumpIRDirectory && printModule && disposeMessage) {
        char *moduleText = printModule(module);
        if (moduleText) {
            char dumpPath[PATH_MAX];
            snprintf(dumpPath, sizeof(dumpPath), "%s/%s.ll", dumpIRDirectory,
                     [function name].UTF8String ?: "unnamed");
            int dumpFD = open(dumpPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if (dumpFD >= 0) {
                (void)write(dumpFD, moduleText, strlen(moduleText));
                close(dumpFD);
            }
            disposeMessage(moduleText);
        }
    }
    // The real PVG plugin performs its own AIR downgrade through
    // PVGPluginGPUCompilerBridge, which restores this deployment target after
    // the downgrade. Setting it here also keeps the module valid if the plugin
    // inspects the target before that internal pass.
    setTarget(module, PVG_AIR_TARGET);

    void *compiler = createCompiler();
    if (!compiler) {
        pvgCompilerTrace("compiler allocation failure");
        disposeModule(module);
        disposeBuffer(input);
        disposeContext(context);
        return function;
    }
    const void *compiledBytes = NULL;
    size_t compiledLength = 0;
    void *unusedData1 = NULL, *unusedData2 = NULL;
    size_t unusedLength1 = 0, unusedLength2 = 0;
    const char *compilerError = NULL;
    int result = buildRequest(
        compiler, NULL, 0, 0, module, &compiledBytes, &compiledLength,
        &unusedData1, &unusedLength1, &unusedData2, &unusedLength2,
        &compilerError);
    NSData *compiled = nil;
    if (result == 0 && compiledBytes && compiledLength)
        compiled = [NSData dataWithBytes:compiledBytes length:compiledLength];
    releaseReply(compiler);
    deleteCompiler(compiler);
    // A successful real-plugin request consumes the module through
    // LLVMExtraMakeSharedModule. This is the same ownership boundary enforced
    // by PVGCompilerPluginBridge; disposing it again crashes in ~Module().
    if (result != 0)
        disposeModule(module);
    disposeBuffer(input);
    disposeContext(context);
    if (!compiled) {
        pvgCompilerTrace("build request failure");
        return function;
    }
    pvgCompilerTrace("built metallib");

    const char *dumpPath = getenv("PVG_DUMP_COMPILED_METALLIB");
    if (dumpPath && *dumpPath)
        [compiled writeToFile:[NSString stringWithUTF8String:dumpPath]
                   atomically:YES];

    void *ownedBytes = malloc(compiled.length);
    if (!ownedBytes)
        return function;
    memcpy(ownedBytes, compiled.bytes, compiled.length);
    dispatch_data_t data = dispatch_data_create(
        ownedBytes, compiled.length,
        dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0),
        DISPATCH_DATA_DESTRUCTOR_FREE);
    NSError *libraryError = nil;
    id<MTLLibrary> library = [device newLibraryWithData:data
                                                   error:&libraryError];
    dispatch_release(data);
    if (!library) {
        pvgCompilerTrace("compiled library rejected");
        return function;
    }
    pvgCompilerTrace("compiled library accepted");
    id executableFunction = [library newFunctionWithName:[function name]];
    [library release];
    if (!executableFunction)
        pvgCompilerTrace("compiled function missing");
    else {
        pvgWriteShaderCache(cachePath, compiled);
        objc_setAssociatedObject(function, &executableKey, executableFunction,
                                  OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        [executableFunction autorelease];
    }
    return executableFunction ?: function;
    }
}

static id pvgNewComputePipeline(id self, SEL selector, id function,
                                NSError **error) {
    return originalNewComputePipeline(
        self, selector, pvgExecutableFunction(self, function), error);
}

static id pvgNewComputePipelineOptions(
    id self, SEL selector, id function, MTLPipelineOption options,
    MTLComputePipelineReflection **reflection, NSError **error) {
    return originalNewComputePipelineOptions(
        self, selector, pvgExecutableFunction(self, function), options,
        reflection, error);
}

static void pvgNewComputePipelineAsync(
    id self, SEL selector, id function,
    MTLNewComputePipelineStateCompletionHandler completionHandler) {
    pvgPrepareAsyncPipeline(pvgColdFunction(function), ^{
    originalNewComputePipelineAsync(
        self, selector, pvgExecutableFunction(self, function),
        completionHandler);
    });
}

static void pvgNewComputePipelineOptionsAsync(
    id self, SEL selector, id function, MTLPipelineOption options,
    MTLNewComputePipelineStateWithReflectionCompletionHandler
        completionHandler) {
    pvgPrepareAsyncPipeline(pvgColdFunction(function), ^{
    originalNewComputePipelineOptionsAsync(
        self, selector, pvgExecutableFunction(self, function), options,
        completionHandler);
    });
}

static MTLComputePipelineDescriptor *pvgCompatibleComputeDescriptor(
    id device, MTLComputePipelineDescriptor *descriptor) {
    if (!descriptor ||
        strcmp(object_getClassName(device), "AppleParavirtDevice") != 0)
        return nil;
    MTLComputePipelineDescriptor *compatible = [descriptor copy];
    compatible.computeFunction =
        pvgExecutableFunction(device, descriptor.computeFunction);
    return compatible;
}

static id pvgNewComputeDescriptorOptions(
    id self, SEL selector, MTLComputePipelineDescriptor *descriptor,
    MTLPipelineOption options, MTLComputePipelineReflection **reflection,
    NSError **error) {
    MTLComputePipelineDescriptor *compatible =
        pvgCompatibleComputeDescriptor(self, descriptor);
    if (!compatible)
        return originalNewComputeDescriptorOptions(
            self, selector, descriptor, options, reflection, error);
    id pipeline = originalNewComputeDescriptorOptions(
        self, selector, compatible, options, reflection, error);
    [compatible release];
    return pipeline;
}

static void pvgNewComputeDescriptorOptionsAsync(
    id self, SEL selector, MTLComputePipelineDescriptor *descriptor,
    MTLPipelineOption options,
    MTLNewComputePipelineStateWithReflectionCompletionHandler
        completionHandler) {
    BOOL cold=pvgColdFunction(descriptor.computeFunction);
    MTLComputePipelineDescriptor *snapshot=cold ? [descriptor copy] : nil;
    MTLComputePipelineDescriptor *input=snapshot ?: descriptor;
    pvgPrepareAsyncPipeline(cold, ^{
    MTLComputePipelineDescriptor *compatible =
        pvgCompatibleComputeDescriptor(self, input);
    originalNewComputeDescriptorOptionsAsync(
        self, selector, compatible ?: input, options, completionHandler);
    [compatible release];
    });
    [snapshot release];
}

static MTLRenderPipelineDescriptor *pvgCompatibleRenderDescriptor(
    id device, MTLRenderPipelineDescriptor *descriptor) {
    if (!descriptor ||
        strcmp(object_getClassName(device), "AppleParavirtDevice") != 0)
        return nil;
    MTLRenderPipelineDescriptor *compatible = [descriptor copy];
    compatible.vertexFunction =
        pvgExecutableFunction(device, descriptor.vertexFunction);
    compatible.fragmentFunction =
        pvgExecutableFunction(device, descriptor.fragmentFunction);
    return compatible;
}

static id pvgNewRenderPipeline(id self, SEL selector,
                               MTLRenderPipelineDescriptor *descriptor,
                               NSError **error) {
    MTLRenderPipelineDescriptor *compatible =
        pvgCompatibleRenderDescriptor(self, descriptor);
    if (!compatible)
        return originalNewRenderPipeline(self, selector, descriptor, error);
    id pipeline = originalNewRenderPipeline(self, selector, compatible, error);
    [compatible release];
    return pipeline;
}

static id pvgNewRenderPipelineOptions(
    id self, SEL selector, MTLRenderPipelineDescriptor *descriptor,
    MTLPipelineOption options, MTLRenderPipelineReflection **reflection,
    NSError **error) {
    MTLRenderPipelineDescriptor *compatible =
        pvgCompatibleRenderDescriptor(self, descriptor);
    if (!compatible)
        return originalNewRenderPipelineOptions(
            self, selector, descriptor, options, reflection, error);
    id pipeline = originalNewRenderPipelineOptions(
        self, selector, compatible, options, reflection, error);
    [compatible release];
    return pipeline;
}

static void pvgNewRenderPipelineAsync(
    id self, SEL selector, MTLRenderPipelineDescriptor *descriptor,
    MTLNewRenderPipelineStateCompletionHandler completionHandler) {
    BOOL cold=pvgColdFunction(descriptor.vertexFunction) || pvgColdFunction(descriptor.fragmentFunction);
    MTLRenderPipelineDescriptor *snapshot=cold ? [descriptor copy] : nil;
    MTLRenderPipelineDescriptor *input=snapshot ?: descriptor;
    pvgPrepareAsyncPipeline(cold, ^{
    MTLRenderPipelineDescriptor *compatible =
        pvgCompatibleRenderDescriptor(self, input);
    originalNewRenderPipelineAsync(
        self, selector, compatible ?: input, completionHandler);
    [compatible release];
    });
    [snapshot release];
}

static void pvgNewRenderPipelineOptionsAsync(
    id self, SEL selector, MTLRenderPipelineDescriptor *descriptor,
    MTLPipelineOption options,
    MTLNewRenderPipelineStateWithReflectionCompletionHandler
        completionHandler) {
    BOOL cold=pvgColdFunction(descriptor.vertexFunction) || pvgColdFunction(descriptor.fragmentFunction);
    MTLRenderPipelineDescriptor *snapshot=cold ? [descriptor copy] : nil;
    MTLRenderPipelineDescriptor *input=snapshot ?: descriptor;
    pvgPrepareAsyncPipeline(cold, ^{
    MTLRenderPipelineDescriptor *compatible =
        pvgCompatibleRenderDescriptor(self, input);
    originalNewRenderPipelineOptionsAsync(
        self, selector, compatible ?: input, options, completionHandler);
    [compatible release];
    });
    [snapshot release];
}

static IMP pvgInstallPipelineHook(Class deviceClass, const char *selectorName,
                                  IMP replacement) {
    SEL selector = sel_registerName(selectorName);
    Method method = class_getInstanceMethod(deviceClass, selector);
    if (!method || method_getImplementation(method) == replacement)
        return NULL;
    IMP original = method_getImplementation(method);
    if (!class_addMethod(deviceClass, selector, replacement,
                         method_getTypeEncoding(method)))
        method_setImplementation(method, replacement);
    pvgCompilerTrace(selectorName);
    return original;
}

__attribute__((visibility("default")))
const char *PVGMetalCompatBuildIdentifier(void) {
    return "PVGMetalCompat-precise-access-and-async-conversion";
}

__attribute__((visibility("default")))
void PVGInstallMetalCompatPipelineHooks(void) {
    Class deviceClass = objc_getClass("AppleParavirtDevice");
    pvgCompilerTrace(deviceClass ? "device class found" : "device class missing");
    if (!deviceClass)
        return;
    extern void PVGInstallWritability(void);
    PVGInstallWritability();

#define PVG_INSTALL_PIPELINE_HOOK(selectorName, original, replacement, type) \
    do {                                                                    \
        IMP implementation = pvgInstallPipelineHook(                        \
            deviceClass, selectorName, (IMP)replacement);                   \
        if (implementation)                                                 \
            original = (type)implementation;                                \
    } while (0)
    PVG_INSTALL_PIPELINE_HOOK(
        "newComputePipelineStateWithFunction:error:",
        originalNewComputePipeline, pvgNewComputePipeline,
        MTLNewComputePipelineFn);
    PVG_INSTALL_PIPELINE_HOOK(
        "newComputePipelineStateWithFunction:options:reflection:error:",
        originalNewComputePipelineOptions, pvgNewComputePipelineOptions,
        MTLNewComputePipelineOptionsFn);
    PVG_INSTALL_PIPELINE_HOOK(
        "newComputePipelineStateWithFunction:completionHandler:",
        originalNewComputePipelineAsync, pvgNewComputePipelineAsync,
        MTLNewComputePipelineAsyncFn);
    PVG_INSTALL_PIPELINE_HOOK(
        "newComputePipelineStateWithFunction:options:completionHandler:",
        originalNewComputePipelineOptionsAsync,
        pvgNewComputePipelineOptionsAsync,
        MTLNewComputePipelineOptionsAsyncFn);
    PVG_INSTALL_PIPELINE_HOOK(
        "newComputePipelineStateWithDescriptor:options:reflection:error:",
        originalNewComputeDescriptorOptions, pvgNewComputeDescriptorOptions,
        MTLNewComputeDescriptorOptionsFn);
    PVG_INSTALL_PIPELINE_HOOK(
        "newComputePipelineStateWithDescriptor:options:completionHandler:",
        originalNewComputeDescriptorOptionsAsync,
        pvgNewComputeDescriptorOptionsAsync,
        MTLNewComputeDescriptorOptionsAsyncFn);
    PVG_INSTALL_PIPELINE_HOOK(
        "newRenderPipelineStateWithDescriptor:error:",
        originalNewRenderPipeline, pvgNewRenderPipeline,
        MTLNewRenderPipelineFn);
    PVG_INSTALL_PIPELINE_HOOK(
        "newRenderPipelineStateWithDescriptor:options:reflection:error:",
        originalNewRenderPipelineOptions, pvgNewRenderPipelineOptions,
        MTLNewRenderPipelineOptionsFn);
    PVG_INSTALL_PIPELINE_HOOK(
        "newRenderPipelineStateWithDescriptor:completionHandler:",
        originalNewRenderPipelineAsync, pvgNewRenderPipelineAsync,
        MTLNewRenderPipelineAsyncFn);
    PVG_INSTALL_PIPELINE_HOOK(
        "newRenderPipelineStateWithDescriptor:options:completionHandler:",
        originalNewRenderPipelineOptionsAsync,
        pvgNewRenderPipelineOptionsAsync,
        MTLNewRenderPipelineOptionsAsyncFn);
#undef PVG_INSTALL_PIPELINE_HOOK
}

@implementation AppleParavirtDevice (PVGMetalCompatLoad)
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 101500
// Descriptor serialization is a separate, Mojave-only implementation.
- (id)serializeRenderPipelineDescriptor:(MTLRenderPipelineDescriptor *)descriptor {
    extern dispatch_data_t PVGMojaveSerializeRender(MTLRenderPipelineDescriptor *);
    return (id)PVGMojaveSerializeRender(descriptor);
}
- (id)serializeComputePipelineDescriptor:(MTLComputePipelineDescriptor *)descriptor {
    extern dispatch_data_t PVGMojaveSerializeCompute(MTLComputePipelineDescriptor *);
    return (id)PVGMojaveSerializeCompute(descriptor);
}
// Mojave's MTLCompiler calls these older selectors. Its _MTLDevice base
// implementations just return nil; the donor only implements the new names.
// Old variant getters return +0; the donor's "new" selectors return +1.
#define PVG_MOJAVE_VARIANT(kind, NewKind) \
- (id)kind##VariantWithCompilerOutput:(id)output pipelineStatisticsOutput:(id)statistics { \
    id (*send)(id, SEL, id, id) = (void *)objc_msgSend; \
    return [send(self, sel_registerName("new" #NewKind "VariantWithCompilerOutput:pipelineStatisticsOutput:"), output, statistics) autorelease]; \
}
PVG_MOJAVE_VARIANT(compute, Compute)
PVG_MOJAVE_VARIANT(vertex, Vertex)
PVG_MOJAVE_VARIANT(fragment, Fragment)
#undef PVG_MOJAVE_VARIANT
- (id)newComputePipelineWithDescriptor:(id)descriptor variant:(id)variant {
    id errorMessage = nil;
    id (*send)(id, SEL, id, id, id *) = (void *)objc_msgSend;
    return send(self, sel_registerName("newComputePipelineWithDescriptor:variant:errorMessage:"),
                descriptor, variant, &errorMessage);
}
- (id)newRenderPipelineWithDescriptor:(id)descriptor vertexVariant:(id)vertex fragmentVariant:(id)fragment {
    id errorMessage = nil;
    id (*send)(id, SEL, id, id, id, id *) = (void *)objc_msgSend;
    return send(self, sel_registerName("newRenderPipelineWithDescriptor:vertexVariant:fragmentVariant:errorMessage:"),
                descriptor, vertex, fragment, &errorMessage);
}
#endif
+ (void)load {
    pvgCompilerTrace("AppleParavirtDevice category +load");
    pvgInstallTextureUsageHooks();
    PVGInstallMetalCompatPipelineHooks();
#if PVG_TRACE_COMMANDS
    PVGInstallCommandTrace();
#endif
#if PVG_SKIP_WINDOWSERVER_MIPMAPS
    PVGInstallWindowServerMipmapProbe();
#endif
}
@end

static void pvgRepairIOSurfaceTextureWireDescriptor(void *args,
                                                    unsigned int argsSize) {
    if (!args || argsSize != 0xa8 || !pvgCurrentTextureDescriptor)
        return;

    uint8_t *bytes = (uint8_t *)args;
    // AppleParavirtTexture appends the allocator's complete 0x30-byte opcode
    // 0x0c record at +0x70. reims-vgpu's independently perturbed oracle fixes
    // the embedded descriptor layout: object ref at +0x78, then packed texture
    // fields at +0x7c through +0x9b and the IOSurface plane at +0x9c.
    if (*(uint32_t *)(bytes + 0x70) != UINT32_C(0x0c) ||
        *(uint32_t *)(bytes + 0x74) != UINT32_C(0x30))
        return;

    MTLTextureDescriptor *descriptor = pvgCurrentTextureDescriptor;
    uint32_t packed =
        ((uint32_t)descriptor.textureType & UINT32_C(0x0f)) |
        (descriptor.allowGPUOptimizedContents ? UINT32_C(0x40) : 0) |
        (((uint32_t)descriptor.usage & UINT32_C(0xff)) << 8) |
        (((uint32_t)descriptor.pixelFormat & UINT32_C(0xffff)) << 16);
    *(uint32_t *)(bytes + 0x7c) = packed;
    *(uint32_t *)(bytes + 0x80) = (uint32_t)descriptor.width;
    *(uint32_t *)(bytes + 0x84) = (uint32_t)descriptor.height;
    *(uint32_t *)(bytes + 0x88) = (uint32_t)descriptor.depth;
    *(uint16_t *)(bytes + 0x8c) = (uint16_t)descriptor.mipmapLevelCount;
    *(uint16_t *)(bytes + 0x8e) = (uint16_t)descriptor.sampleCount;
    *(uint16_t *)(bytes + 0x90) = (uint16_t)descriptor.arrayLength;
    *(uint16_t *)(bytes + 0x92) = (uint16_t)descriptor.resourceOptions;

    // +0x60 belongs to the base IOAccel resource descriptor, immediately
    // before AppleParavirtTexture's opcode-0x0c record. Catalina sometimes
    // leaves an address-like value here on the second WindowServer
    // full-screen IOSurface while the first surface is zero. Native Big Sur
    // emits zero for both its full-screen WindowServer IOSurface and all
    // independently captured IOSurface textures. Do not let Catalina private
    // object state escape into the donor kernel descriptor.
    *(uint64_t *)(bytes + 0x60) = 0;

    // The serializer's declared trailing u64 is written as zero by native Big
    // Sur for plain, heap, buffer-backed, and IOSurface-backed textures. The
    // donor serializer reads it from the wrong Catalina descriptor offsets and
    // otherwise leaks stack bytes here, which the host rejects as an invalid
    // IOSurface texture descriptor.
    *(uint64_t *)(bytes + 0x94) = 0;
    *(uint16_t *)(bytes + 0x9c) = (uint16_t)pvgCurrentTexturePlane;
    *(uint16_t *)(bytes + 0x9e) = 0;
}

static id pvgResourceInit(id self, SEL selector, id device,
                          id remoteStorageResource, unsigned long options,
                          void *args, unsigned int argsSize) {
#if PVG_TRACE_RESOURCES
    uint32_t traceSequence = __sync_add_and_fetch(&pvgResourceTraceCount, 1);
    pvgTraceResource("before", traceSequence, nil, remoteStorageResource,
                     options, args, argsSize);
#endif
    // Every AppleParavirtTexture create-descriptor variant packs its resource
    // type and usage at +0x7c differently on the two OS releases. Big Sur
    // records `0x40 | MTLTextureType | (MTLTextureUsage << 8)`; Catalina's
    // MTLIOAccelTexture base always leaves 0x42 in the low byte, mislabeling
    // arrays and other non-2D textures as MTLTextureType2D. The serialized PVG
    // command stream is otherwise byte-identical, but the malformed resource
    // makes the host reject (or crash on) the first render Exec. WindowServer
    // also creates a 0x198-byte view/backing variant for the menu bar; limiting
    // this repair to the common 0xe4-byte form left that texture at 0x00500052
    // instead of the native Big Sur 0x00500542. The packed field has the same
    // offset in every observed variant, so keep the repair strictly to the
    // imported texture object/device and the minimum containing size.
    if (args && argsSize >= 0x80 && device && self &&
        strcmp(object_getClassName(device), "AppleParavirtDevice") == 0 &&
        strcmp(object_getClassName(self), "AppleParavirtTexture") == 0) {
        // The outer AppleParavirtTexture initializer still has the source
        // MTLTextureDescriptor, while this base initializer receives only the
        // serialized IOAccel block. Carry its real type and usage through
        // thread-local state. Offset +0x78 is a resource ID, not usage; using
        // it accidentally turns resource ID bit 0x20 into ShaderAtomic on
        // newer hosts and makes ordinary RGBA8 textures fail validation.
        if (pvgHasTextureDescriptor) {
            uint16_t *textureFlags = (uint16_t *)((uint8_t *)args + 0x7c);
            *textureFlags = (pvgCurrentTextureDescriptor.allowGPUOptimizedContents
                                ? UINT16_C(0x0040) : 0) |
                (uint16_t)pvgCurrentTextureType |
                (uint16_t)((pvgCurrentTextureUsage & UINT8_MAX) << 8);
        }
        if (argsSize >= 0x9c &&
            *(uint32_t *)((uint8_t *)args + 0x70) == UINT32_C(0x01) &&
            *(uint32_t *)((uint8_t *)args + 0x74) == UINT32_C(0x2c)) {
            // Catalina's base serializer drops resourceOptions here: a
            // requested private texture (0x20) arrives on the wire as shared
            // (0). Color clears can hide this; host depth/stencil creation
            // aborts. Use the live API descriptor, as for IOSurface textures.
            if (pvgCurrentTextureDescriptor)
                *(uint16_t *)((uint8_t *)args + 0x92) =
                    (uint16_t)pvgCurrentTextureDescriptor.resourceOptions;
            // The 0x2c-byte create-texture record ends with this u64. The
            // surrounding allocation descriptor has variable length: e.g.
            // a 64x64 texture with seven mip levels is 0x1bc, not 0xe4 or
            // 0x198. Key the repair to the wire record, never those aggregate
            // sizes. Native Big Sur emits zero; Catalina otherwise leaks
            // legacy descriptor/stack bytes and faults even a base-level
            // clear. Leave the following allocator metadata intact.
            *(uint32_t *)((uint8_t *)args + 0x94) = 0;
            *(uint32_t *)((uint8_t *)args + 0x98) = 0;
        }
        if (!pvgCurrentTextureDescriptor && argsSize == 0x198 &&
            *(uint32_t *)((uint8_t *)args + 0x00) == UINT32_C(0x80)) {
            // The larger WindowServer view/backing descriptor has one more
            // finite Catalina/Big Sur ABI difference. At the actual
            // IOAccelResourceCreate boundary, native Big Sur emits 0x100001
            // at +0x90 for both a compositor-sized texture and a 54x54
            // texture, while Catalina emits 0x000001. Preserve the existing
            // low flags and supply the missing client-mapping bit. Without it
            // Parallels accepts resource creation but faults the first Exec
            // that samples the texture.
            *(uint32_t *)((uint8_t *)args + 0x90) |= UINT32_C(0x100000);
        }
    }

    if (args && argsSize == 0xa8 && device && self &&
        strcmp(object_getClassName(device), "AppleParavirtDevice") == 0 &&
        strcmp(object_getClassName(self), "AppleParavirtTexture") == 0 &&
        pvgHasTextureDescriptor) {
        pvgRepairIOSurfaceTextureWireDescriptor(args, argsSize);
    }

    // Keep Catalina's layout intact until its base class has allocated client
    // storage. The rebound IOAccelResourceCreate translates a private copy at
    // the final user/kernel boundary, after all old-layout fields are valid.
    BOOL translateResourceABI = device &&
        strcmp(object_getClassName(device), "AppleParavirtDevice") == 0;
    if (translateResourceABI)
        ++pvgResourceABITranslationDepth;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 101500
    id result = originalMojaveResourceInit(self, selector, device, options,
                                          args, argsSize);
#else
    id result = originalResourceInit(self, selector, device,
                                     remoteStorageResource, options, args,
                                     argsSize);
#endif
    if (translateResourceABI)
        --pvgResourceABITranslationDepth;
#if PVG_TRACE_RESOURCES
    pvgTraceResource("after", traceSequence, result, remoteStorageResource,
                     options, args, argsSize);
#endif
    return result;
}

#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 101500
// Mojave's native initializer predates the remoteStorageResource argument.
// Preserve its calling convention while sharing the descriptor translation.
static id pvgMojaveResourceInit(id self, SEL selector, id device,
                               unsigned long options, void *args,
                               unsigned int argsSize) {
    return pvgResourceInit(self, selector, device, nil, options, args, argsSize);
}
#endif

__attribute__((constructor)) static void installPVGResourceABIAdapter(void) {
    (void)pvgInstallIOAccelDisplayCompletionABIRepair();
    pvgInstallIOAccelCreateTrace();
    pvgCompilerTrace("constructor");
    PVGInstallMetalCompatPipelineHooks();

    // MetalSerializer can be preloaded before the first resource object is
    // constructed.  Resource translation is then simply unavailable for that
    // process; it must not prevent the independent pipeline hooks above from
    // protecting source-compiled functions.
    Class resourceClass = objc_getClass("MTLIOAccelResource");
    SEL selector = sel_registerName(
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 101500
        "initWithDevice:options:args:argsSize:");
#else
        "initWithDevice:remoteStorageResource:options:args:argsSize:");
#endif
    Method method = resourceClass ? class_getInstanceMethod(resourceClass,
                                                             selector)
                                  : NULL;
    if (method) {
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 101500
        originalMojaveResourceInit =
            (PVGMojaveResourceInitFn)method_getImplementation(method);
        method_setImplementation(method, (IMP)pvgMojaveResourceInit);
#else
        originalResourceInit =
            (MTLResourceInitFn)method_getImplementation(method);
        method_setImplementation(method, (IMP)pvgResourceInit);
#endif
    }
}
