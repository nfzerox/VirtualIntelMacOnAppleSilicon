#import <Foundation/Foundation.h>
#import <IOSurface/IOSurface.h>

#include <fcntl.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <objc/runtime.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef id (*ResourceInitFn)(id, SEL, id, id, unsigned long, void *,
                             unsigned int);
typedef id (*TextureIOSurfaceInitFn)(id, SEL, id, id, IOSurfaceRef,
                                     unsigned int, unsigned int, void *,
                                     unsigned int);
typedef void *(*IOAccelResourceCreateFn)(void *, void *, uint32_t);

static ResourceInitFn originalResourceInit;
static TextureIOSurfaceInitFn originalTextureIOSurfaceInit;
static IOAccelResourceCreateFn originalIOAccelResourceCreate;
static unsigned nextSequence;
static BOOL hookInstalled;

__attribute__((visibility("default"))) void PVGInstallResourceTrace(void);

__attribute__((constructor)) static void autoInstallResourceTrace(void) {
    if (getenv("PVG_RESOURCE_TRACE_AUTO"))
        PVGInstallResourceTrace();
}

static void appendManifestLine(int fd, const char *format, ...) {
    char line[512];
    va_list arguments;
    va_start(arguments, format);
    int length = vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    if (length > 0) {
        size_t count = (size_t)length < sizeof(line)
            ? (size_t)length : sizeof(line) - 1;
        (void)write(fd, line, count);
    }
}

static void dumpArgs(unsigned sequence, const char *phase, const void *args,
                     unsigned int length) {
    if (!args || !length)
        return;
    char directory[128];
    snprintf(directory, sizeof(directory), "/tmp/pvg-resource-binary-%d",
             getpid());
    (void)mkdir(directory, 0777);
    char path[192];
    snprintf(path, sizeof(path), "%s/%06u-%s-%u.bin", directory, sequence,
             phase, length);
    int output = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (output >= 0) {
        (void)write(output, args, length);
        close(output);
    }
}

static id tracedResourceInit(id self, SEL selector, id device,
                             id remoteStorageResource, unsigned long options,
                             void *args, unsigned int argsSize) {
    unsigned sequence = __sync_fetch_and_add(&nextSequence, 1);
    dumpArgs(sequence, "before", args, argsSize);
    id result = originalResourceInit(self, selector, device,
                                     remoteStorageResource, options, args,
                                     argsSize);
    dumpArgs(sequence, "after", args, argsSize);

    char manifestPath[160];
    snprintf(manifestPath, sizeof(manifestPath),
             "/tmp/pvg-resource-binary-%d/manifest.txt", getpid());
    int manifest = open(manifestPath, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (manifest >= 0) {
        appendManifestLine(
            manifest,
            "resource sequence=%u self=%s result=%s remote=%s options=0x%lx "
            "size=%u\n",
            sequence, self ? object_getClassName(self) : "(null)",
            result ? object_getClassName(result) : "(null)",
            remoteStorageResource
                ? object_getClassName(remoteStorageResource) : "(null)",
            options, argsSize);
        close(manifest);
    }
    return result;
}

static id tracedTextureIOSurfaceInit(id self, SEL selector, id device,
                                     id descriptor, IOSurfaceRef surface,
                                     unsigned int plane, unsigned int field,
                                     void *args, unsigned int argsSize) {
    unsigned sequence = __sync_fetch_and_add(&nextSequence, 1);
    dumpArgs(sequence, "texture-iosurface-before", args, argsSize);
    id result = originalTextureIOSurfaceInit(
        self, selector, device, descriptor, surface, plane, field, args,
        argsSize);
    dumpArgs(sequence, "texture-iosurface-after", args, argsSize);

    char manifestPath[160];
    snprintf(manifestPath, sizeof(manifestPath),
             "/tmp/pvg-resource-binary-%d/manifest.txt", getpid());
    int manifest = open(manifestPath, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (manifest >= 0) {
        appendManifestLine(
            manifest,
            "texture-iosurface sequence=%u self=%s result=%s surface=%u "
            "plane=%u field=%u size=%u\n",
            sequence, self ? object_getClassName(self) : "(null)",
            result ? object_getClassName(result) : "(null)",
            surface ? IOSurfaceGetID(surface) : 0, plane, field, argsSize);
        close(manifest);
    }
    return result;
}

static void *tracedIOAccelResourceCreate(void *sharedRef, void *args,
                                         uint32_t argsSize) {
    if (!originalIOAccelResourceCreate)
        originalIOAccelResourceCreate = (IOAccelResourceCreateFn)dlsym(
            RTLD_NEXT, "IOAccelResourceCreate");
    if (!originalIOAccelResourceCreate)
        return NULL;
    unsigned sequence = __sync_fetch_and_add(&nextSequence, 1);
    dumpArgs(sequence, "kernel-before", args, argsSize);
    void *result = originalIOAccelResourceCreate(sharedRef, args, argsSize);
    dumpArgs(sequence, result ? "kernel-after-ok" : "kernel-after-nil", args,
             argsSize);

    char manifestPath[160];
    snprintf(manifestPath, sizeof(manifestPath),
             "/tmp/pvg-resource-binary-%d/manifest.txt", getpid());
    int manifest = open(manifestPath, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (manifest >= 0) {
        uint32_t resourceType = 0;
        if (args && argsSize >= sizeof(resourceType))
            memcpy(&resourceType, args, sizeof(resourceType));
        appendManifestLine(
            manifest,
            "kernel sequence=%u shared=%p result=%p size=%u type=0x%x\n",
            sequence, sharedRef, result, argsSize, resourceType);
        close(manifest);
    }
    return result;
}

static void rebindIOAccelResourceCreate(const struct mach_header *header,
                                        intptr_t slide) {
    if (!header || header->magic != MH_MAGIC_64)
        return;
    const struct mach_header_64 *header64 =
        (const struct mach_header_64 *)header;
    const struct segment_command_64 *linkedit = NULL;
    const struct symtab_command *symtab = NULL;
    const struct dysymtab_command *dysymtab = NULL;
    const uint8_t *commands = (const uint8_t *)(header64 + 1);
    const struct load_command *command =
        (const struct load_command *)commands;
    for (uint32_t index = 0; index < header64->ncmds; ++index) {
        if (command->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *segment =
                (const struct segment_command_64 *)command;
            if (strcmp(segment->segname, SEG_LINKEDIT) == 0)
                linkedit = segment;
        } else if (command->cmd == LC_SYMTAB) {
            symtab = (const struct symtab_command *)command;
        } else if (command->cmd == LC_DYSYMTAB) {
            dysymtab = (const struct dysymtab_command *)command;
        }
        command = (const struct load_command *)((const uint8_t *)command +
                                                 command->cmdsize);
    }
    if (!linkedit || !symtab || !dysymtab)
        return;

    uintptr_t linkeditBase =
        (uintptr_t)slide + linkedit->vmaddr - linkedit->fileoff;
    const struct nlist_64 *symbols =
        (const struct nlist_64 *)(linkeditBase + symtab->symoff);
    const char *strings = (const char *)(linkeditBase + symtab->stroff);
    const uint32_t *indirect =
        (const uint32_t *)(linkeditBase + dysymtab->indirectsymoff);

    command = (const struct load_command *)commands;
    for (uint32_t commandIndex = 0; commandIndex < header64->ncmds;
         ++commandIndex) {
        if (command->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *segment =
                (const struct segment_command_64 *)command;
            const struct section_64 *section =
                (const struct section_64 *)(segment + 1);
            for (uint32_t sectionIndex = 0; sectionIndex < segment->nsects;
                 ++sectionIndex, ++section) {
                uint32_t type = section->flags & SECTION_TYPE;
                if (type != S_LAZY_SYMBOL_POINTERS &&
                    type != S_NON_LAZY_SYMBOL_POINTERS)
                    continue;
                void **bindings =
                    (void **)((uintptr_t)slide + section->addr);
                size_t count = section->size / sizeof(void *);
                for (size_t bindingIndex = 0; bindingIndex < count;
                     ++bindingIndex) {
                    uint32_t symbolIndex =
                        indirect[section->reserved1 + bindingIndex];
                    if (symbolIndex == INDIRECT_SYMBOL_ABS ||
                        symbolIndex == INDIRECT_SYMBOL_LOCAL ||
                        symbolIndex == (INDIRECT_SYMBOL_LOCAL |
                                        INDIRECT_SYMBOL_ABS))
                        continue;
                    const char *name =
                        strings + symbols[symbolIndex].n_un.n_strx;
                    if (strcmp(name, "_IOAccelResourceCreate") != 0)
                        continue;
                    if (bindings[bindingIndex] ==
                        (void *)tracedIOAccelResourceCreate)
                        continue;
                    if (!originalIOAccelResourceCreate)
                        originalIOAccelResourceCreate =
                            (IOAccelResourceCreateFn)bindings[bindingIndex];
                    mach_vm_address_t page =
                        (mach_vm_address_t)(uintptr_t)&bindings[bindingIndex] &
                        ~((mach_vm_address_t)vm_page_size - 1);
                    if (mach_vm_protect(mach_task_self(), page, vm_page_size,
                                        FALSE,
                                        VM_PROT_READ | VM_PROT_WRITE |
                                            VM_PROT_COPY) == KERN_SUCCESS) {
                        bindings[bindingIndex] =
                            (void *)tracedIOAccelResourceCreate;
                    }
                }
            }
        }
        command = (const struct load_command *)((const uint8_t *)command +
                                                 command->cmdsize);
    }
}

__attribute__((visibility("default"))) void PVGInstallResourceTrace(void) {
    if (hookInstalled)
        return;
    originalIOAccelResourceCreate = (IOAccelResourceCreateFn)dlsym(
        RTLD_DEFAULT, "IOAccelResourceCreate");
    _dyld_register_func_for_add_image(rebindIOAccelResourceCreate);
    Class resourceClass = objc_getClass("MTLIOAccelResource");
    SEL selector = sel_registerName(
        "initWithDevice:remoteStorageResource:options:args:argsSize:");
    Method method = resourceClass
        ? class_getInstanceMethod(resourceClass, selector) : NULL;
    if (method && method_getImplementation(method) != (IMP)tracedResourceInit) {
        originalResourceInit =
            (ResourceInitFn)method_getImplementation(method);
        method_setImplementation(method, (IMP)tracedResourceInit);
        hookInstalled = YES;
    }

    Class textureClass = objc_getClass("MTLIOAccelTexture");
    SEL textureSelector = sel_registerName(
        "initWithDevice:descriptor:iosurface:plane:field:args:argsSize:");
    Method textureMethod = textureClass
        ? class_getInstanceMethod(textureClass, textureSelector) : NULL;
    if (textureMethod && method_getImplementation(textureMethod) !=
                             (IMP)tracedTextureIOSurfaceInit) {
        originalTextureIOSurfaceInit =
            (TextureIOSurfaceInitFn)method_getImplementation(textureMethod);
        method_setImplementation(textureMethod,
                                 (IMP)tracedTextureIOSurfaceInit);
        hookInstalled = YES;
    }
}
