#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <fcntl.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <execinfo.h>

static id (*originalBlitEncoder)(id, SEL);
static void (*originalReplace)(id, SEL, MTLRegion, NSUInteger, const void *, NSUInteger);
static void tracedReplace(id self, SEL sel, MTLRegion r, NSUInteger level, const void *data, NSUInteger row) {
    fprintf(stderr,"PVG replace texture=%p region=%lu,%lu %lux%lu level=%lu row=%lu data=%p\n",self,r.origin.x,r.origin.y,r.size.width,r.size.height,level,row,data);
    void *frames[12]; int n=backtrace(frames,12); backtrace_symbols_fd(frames,n,STDERR_FILENO);
    originalReplace(self,sel,r,level,data,row);
}
static id tracedBlitEncoder(id self, SEL selector) {
    id result = originalBlitEncoder(self, selector);
    fprintf(stderr, "PVG blit factory buffer=%p encoder=%p class=%s\n", self, result, object_getClassName(result));
    void *frames[16];
    int count = backtrace(frames, 16);
    backtrace_symbols_fd(frames, count, STDERR_FILENO);
    return result;
}

typedef void *(*GetCommandBytesFn)(id, SEL, size_t);
typedef void (*CommitFn)(id, SEL);
typedef BOOL (*AddResourceReferenceFn)(id, SEL, id, BOOL);
typedef BOOL (*AddStateReferenceFn)(id, SEL, id);

typedef struct TraceAllocation {
    id commandBuffer;
    void *bytes;
    size_t length;
    unsigned sequence;
    struct TraceAllocation *next;
} TraceAllocation;

typedef struct TraceReference {
    id commandBuffer;
    const void *object;
    unsigned sequence;
    BOOL accepted;
    BOOL isState;
    BOOL isWrite;
    char className[80];
    uint64_t serializerResourceRef;
    uint64_t textureRef;
    uint64_t bufferRef;
    uint64_t pipelineRef;
    uint64_t samplerRef;
    uint64_t fenceRef;
    uint64_t width;
    uint64_t height;
    uint64_t depth;
    uint64_t pixelFormat;
    uint64_t textureType;
    uint64_t usage;
    uint64_t storageMode;
    uint64_t length;
    struct TraceReference *next;
} TraceReference;

static GetCommandBytesFn originalGetCommandBytes;
static CommitFn originalCommit;
static AddResourceReferenceFn originalAddResourceReference;
static AddStateReferenceFn originalAddStateReference;
static TraceAllocation *allocations;
static TraceReference *references;
static unsigned nextSequence;
static unsigned nextReferenceSequence;
static unsigned commitCount;
static BOOL hooksInstalled;
static pthread_mutex_t traceLock = PTHREAD_MUTEX_INITIALIZER;

static BOOL traceArmed(void) {
#if PVG_TRACE_COMMANDS_ARMED
    static BOOL armed;
    if (!armed && access("/tmp/pvg-command-arm", F_OK) == 0)
        armed = YES;
    return armed;
#else
    return YES;
#endif
}

static uint64_t unsignedProperty(id object, const char *selectorName) {
    SEL selector = sel_registerName(selectorName);
    if (!object || ![object respondsToSelector:selector])
        return 0;
    return ((uint64_t (*)(id, SEL))objc_msgSend)(object, selector);
}

static TraceReference *captureReference(id commandBuffer, id object,
                                        BOOL accepted, BOOL isState,
                                        BOOL isWrite) {
    TraceReference *reference = calloc(1, sizeof(*reference));
    if (!reference)
        return NULL;
    reference->commandBuffer = commandBuffer;
    reference->object = (__bridge const void *)object;
    reference->sequence = __sync_fetch_and_add(&nextReferenceSequence, 1);
    reference->accepted = accepted;
    reference->isState = isState;
    reference->isWrite = isWrite;
    const char *className = object ? object_getClassName(object) : "(null)";
    snprintf(reference->className, sizeof(reference->className), "%s",
             className ? className : "(unknown)");
    reference->serializerResourceRef =
        unsignedProperty(object, "serializerResourceRef");
    reference->textureRef = unsignedProperty(object, "textureRef");
    reference->bufferRef = unsignedProperty(object, "bufferRef");
    reference->pipelineRef = unsignedProperty(object, "pipelineRef");
    reference->samplerRef = unsignedProperty(object, "samplerRef");
    reference->fenceRef = unsignedProperty(object, "fenceRef");
    reference->width = unsignedProperty(object, "width");
    reference->height = unsignedProperty(object, "height");
    reference->depth = unsignedProperty(object, "depth");
    reference->pixelFormat = unsignedProperty(object, "pixelFormat");
    reference->textureType = unsignedProperty(object, "textureType");
    reference->usage = unsignedProperty(object, "usage");
    reference->storageMode = unsignedProperty(object, "storageMode");
    reference->length = unsignedProperty(object, "length");
    return reference;
}

/*
 * WindowServer's first compositor fault occurs after login-time services have
 * already submitted the small bootstrap buffers.  Keep the trace bounded, but
 * retain enough commits to cover the first complete desktop composition pass.
 * A 512-commit run is still only a few tens of megabytes because every copied
 * allocation and object snapshot is capped at 4 KiB.
 */
enum {
#if PVG_TRACE_COMMANDS_ARMED
    PVGCommandTraceCommitLimit = 128
#else
    PVGCommandTraceCommitLimit = 512
#endif
};

#if PVG_TRACE_COMMANDS_ARMED
static void dumpLargeBuffer(const char *directory, unsigned commitSequence,
                            const TraceReference *reference) {
    if (strcmp(reference->className, "AppleParavirtBuffer") != 0 ||
        reference->length != 0x40000 || !reference->bufferRef)
        return;
    id object = (__bridge id)reference->object;
    SEL contentsSelector = sel_registerName("contents");
    if (![object respondsToSelector:contentsSelector])
        return;
    const void *contents = ((const void *(*)(id, SEL))objc_msgSend)(
        object, contentsSelector);
    if (!contents)
        return;
    uint8_t *bytes = malloc((size_t)reference->length);
    if (!bytes)
        return;
    mach_vm_size_t copied = 0;
    kern_return_t result = mach_vm_read_overwrite(
        mach_task_self(), (mach_vm_address_t)(uintptr_t)contents,
        (mach_vm_size_t)reference->length,
        (mach_vm_address_t)(uintptr_t)bytes, &copied);
    if (result == KERN_SUCCESS && copied == reference->length) {
        char path[192];
        snprintf(path, sizeof(path), "%s/commit-%03u-buffer-%llx.bin",
                 directory, commitSequence,
                 (unsigned long long)reference->bufferRef);
        int output = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (output >= 0) {
            (void)write(output, bytes, (size_t)copied);
            close(output);
        }
    }
    free(bytes);
}
#endif

static void dumpReadableMemory(const char *directory, unsigned commitSequence,
                               const char *label, const void *address,
                               size_t requestedLength) {
    if (!address || !requestedLength)
        return;
    if (requestedLength > 4096)
        requestedLength = 4096;

    uint8_t bytes[4096];
    mach_vm_size_t copiedLength = 0;
    kern_return_t result = mach_vm_read_overwrite(
        mach_task_self(), (mach_vm_address_t)(uintptr_t)address,
        (mach_vm_size_t)requestedLength,
        (mach_vm_address_t)(uintptr_t)bytes, &copiedLength);
    if (result != KERN_SUCCESS || copiedLength == 0)
        return;

    char path[192];
    snprintf(path, sizeof(path), "%s/commit-%03u-%s-%p-%llu.bin", directory,
             commitSequence, label, address,
             (unsigned long long)copiedLength);
    int output = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (output >= 0) {
        (void)write(output, bytes, (size_t)copiedLength);
        close(output);
    }
}

static void dumpIvarTarget(const char *directory, unsigned commitSequence,
                           id object, const char *ivarName,
                           size_t requestedLength) {
    Ivar ivar = class_getInstanceVariable(object_getClass(object), ivarName);
    if (!ivar)
        return;
    ptrdiff_t offset = ivar_getOffset(ivar);
    void *target = NULL;
    memcpy(&target, (const uint8_t *)(__bridge const void *)object + offset,
           sizeof(target));
    dumpReadableMemory(directory, commitSequence, ivarName, target,
                       requestedLength);
}

static void appendManifestLine(int fd, const char *format, ...) {
    char line[1024];
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

static void dumpCommandBufferLocked(id commandBuffer, unsigned commitSequence) {
    char directory[128];
    snprintf(directory, sizeof(directory), "/tmp/pvg-command-%d", getpid());
    (void)mkdir(directory, 0777);

    char manifestPath[160];
    snprintf(manifestPath, sizeof(manifestPath), "%s/manifest.txt", directory);
    int manifest = open(manifestPath, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (manifest < 0)
        return;
    struct timeval now;
    gettimeofday(&now, NULL);
    appendManifestLine(manifest,
                       "commit sequence=%u time=%lld.%06d commandBuffer=%p\n",
                       commitSequence, (long long)now.tv_sec,
                       (int)now.tv_usec, commandBuffer);

    dumpReadableMemory(directory, commitSequence, "object",
                       (__bridge const void *)commandBuffer,
                       class_getInstanceSize(object_getClass(commandBuffer)));
    dumpIvarTarget(directory, commitSequence, commandBuffer, "_currentExec",
                   1024);
    dumpIvarTarget(directory, commitSequence, commandBuffer, "_currentChunk",
                   256);
    dumpIvarTarget(directory, commitSequence, commandBuffer, "_storage", 4096);

#if PVG_TRACE_COMMANDS_ARMED
    char recordPath[192];
    snprintf(recordPath, sizeof(recordPath), "%s/commit-%03u-records.bin",
             directory, commitSequence);
    int recordsFile = open(recordPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
#endif
    for (TraceAllocation *allocation = allocations; allocation;
         allocation = allocation->next) {
        if (allocation->commandBuffer != commandBuffer)
            continue;
#if PVG_TRACE_COMMANDS_ARMED
        if (recordsFile < 0)
            continue;
        off_t recordOffset = lseek(recordsFile, 0, SEEK_CUR);
        if (recordOffset < 0 ||
            write(recordsFile, allocation->bytes, allocation->length) !=
                (ssize_t)allocation->length)
            continue;
#else
        char path[192];
        snprintf(path, sizeof(path), "%s/%06u-%zu.bin", directory,
                 allocation->sequence, allocation->length);
        int output = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (output >= 0) {
            (void)write(output, allocation->bytes, allocation->length);
            close(output);
        }
#endif
        uint32_t opcode = 0;
        uint32_t wireLength = 0;
        if (allocation->length >= 8) {
            memcpy(&opcode, allocation->bytes, sizeof(opcode));
            memcpy(&wireLength, (uint8_t *)allocation->bytes + 4,
                   sizeof(wireLength));
        }
        appendManifestLine(manifest,
#if PVG_TRACE_COMMANDS_ARMED
                           "record sequence=%u size=%zu opcode=0x%08x "
                           "wireLength=%u offset=%lld path=%s\n",
                           allocation->sequence, allocation->length, opcode,
                           wireLength, (long long)recordOffset, recordPath);
#else
                           "record sequence=%u size=%zu opcode=0x%08x "
                           "wireLength=%u path=%s\n",
                           allocation->sequence, allocation->length, opcode,
                           wireLength, path);
#endif
    }
#if PVG_TRACE_COMMANDS_ARMED
    if (recordsFile >= 0)
        close(recordsFile);
#endif
    uint64_t savedBufferRefs[16] = {0};
    unsigned savedBufferCount = 0;
    for (TraceReference *reference = references; reference;
         reference = reference->next) {
        if (reference->commandBuffer != commandBuffer)
            continue;
        appendManifestLine(
            manifest,
            "reference sequence=%u kind=%s accepted=%u write=%u object=%p "
            "class=%s serializer=0x%llx texture=0x%llx buffer=0x%llx "
            "pipeline=0x%llx sampler=0x%llx fence=0x%llx "
            "dimensions=%llux%llux%llu format=0x%llx type=0x%llx "
            "usage=0x%llx storage=0x%llx length=0x%llx\n",
            reference->sequence, reference->isState ? "state" : "resource",
            reference->accepted, reference->isWrite, reference->object,
            reference->className, reference->serializerResourceRef,
            reference->textureRef, reference->bufferRef,
            reference->pipelineRef, reference->samplerRef,
            reference->fenceRef, reference->width, reference->height,
            reference->depth, reference->pixelFormat,
            reference->textureType, reference->usage,
            reference->storageMode, reference->length);
        if (getenv("PVG_TRACE_RESOURCE_DESCRIPTION")) {
            id object=(__bridge id)reference->object;
            appendManifestLine(manifest,"description sequence=%u %s\n",reference->sequence,
                               [[object description] UTF8String]);
        }
        char label[64];
#if !PVG_TRACE_COMMANDS_ARMED
        snprintf(label, sizeof(label), "reference-%06u-%s",
                 reference->sequence, reference->className);
        dumpReadableMemory(directory, commitSequence, label,
                           reference->object, 512);
#else
        (void)label;
#endif
#if PVG_TRACE_COMMANDS_ARMED
        if (reference->bufferRef && reference->length == 0x40000 &&
            savedBufferCount < 16) {
            BOOL seen = NO;
            for (unsigned index = 0; index < savedBufferCount; ++index) {
                if (savedBufferRefs[index] == reference->bufferRef) {
                    seen = YES;
                    break;
                }
            }
            if (!seen) {
                savedBufferRefs[savedBufferCount++] = reference->bufferRef;
                dumpLargeBuffer(directory, commitSequence, reference);
            }
        }
#endif
    }
    close(manifest);
}

static void *tracedGetCommandBytes(id self, SEL selector, size_t length) {
    void *bytes = originalGetCommandBytes(self, selector, length);
    if (!traceArmed())
        return bytes;
    pthread_mutex_lock(&traceLock);
    if (commitCount >= PVGCommandTraceCommitLimit) {
        pthread_mutex_unlock(&traceLock);
        return bytes;
    }
    TraceAllocation *allocation = calloc(1, sizeof(*allocation));
    if (allocation) {
        allocation->commandBuffer = self;
        allocation->bytes = bytes;
        allocation->length = length;
        allocation->sequence = __sync_fetch_and_add(&nextSequence, 1);
        allocation->next = allocations;
        allocations = allocation;
    }
    pthread_mutex_unlock(&traceLock);
    return bytes;
}

static BOOL tracedAddResourceReference(id self, SEL selector, id resource,
                                       BOOL isWrite) {
    BOOL accepted = originalAddResourceReference(self, selector, resource,
                                                 isWrite);
    if (!traceArmed())
        return accepted;
    pthread_mutex_lock(&traceLock);
    if (commitCount < PVGCommandTraceCommitLimit) {
        TraceReference *reference = captureReference(self, resource, accepted,
                                                     NO, isWrite);
        if (reference) {
            reference->next = references;
            references = reference;
        }
    }
    pthread_mutex_unlock(&traceLock);
    return accepted;
}

static BOOL tracedAddStateReference(id self, SEL selector, id state) {
    BOOL accepted = originalAddStateReference(self, selector, state);
    if (!traceArmed())
        return accepted;
    pthread_mutex_lock(&traceLock);
    if (commitCount < PVGCommandTraceCommitLimit) {
        TraceReference *reference = captureReference(self, state, accepted,
                                                     YES, NO);
        if (reference) {
            reference->next = references;
            references = reference;
        }
    }
    pthread_mutex_unlock(&traceLock);
    return accepted;
}

static void tracedCommit(id self, SEL selector) {
    if (getenv("PVG_TRACE_COMMIT_STACK")) {
        void *frames[20]; int n=backtrace(frames,20);
        fprintf(stderr,"PVG commit buffer=%p\n",self);
        backtrace_symbols_fd(frames,n,STDERR_FILENO);
    }
    if (!traceArmed()) {
        originalCommit(self, selector);
        return;
    }
    pthread_mutex_lock(&traceLock);
    unsigned commitSequence = commitCount++;
    if (commitSequence < PVGCommandTraceCommitLimit)
        dumpCommandBufferLocked(self, commitSequence);
    TraceAllocation **link = &allocations;
    while (*link) {
        TraceAllocation *allocation = *link;
        if (allocation->commandBuffer == self) {
            *link = allocation->next;
            free(allocation);
        } else {
            link = &allocation->next;
        }
    }
    TraceReference **referenceLink = &references;
    while (*referenceLink) {
        TraceReference *reference = *referenceLink;
        if (reference->commandBuffer == self) {
            *referenceLink = reference->next;
            free(reference);
        } else {
            referenceLink = &reference->next;
        }
    }
    pthread_mutex_unlock(&traceLock);
    originalCommit(self, selector);
}

__attribute__((visibility("default"))) void PVGInstallCommandTrace(void) {
    if (hooksInstalled)
        return;
    Class commandBufferClass = objc_getClass("AppleParavirtCommandBuffer");
    if (!commandBufferClass)
        return;

    SEL getBytesSelector = sel_registerName("getCommandBufferBytes:");
    Method getBytesMethod = class_getInstanceMethod(commandBufferClass,
                                                     getBytesSelector);
    SEL commitSelector = sel_registerName("commit");
    Method commitMethod = class_getInstanceMethod(commandBufferClass,
                                                   commitSelector);
    SEL addResourceSelector =
        sel_registerName("addResourceReference:isWrite:");
    Method addResourceMethod = class_getInstanceMethod(commandBufferClass,
                                                        addResourceSelector);
    SEL addStateSelector = sel_registerName("addStateReference:");
    Method addStateMethod = class_getInstanceMethod(commandBufferClass,
                                                    addStateSelector);
    if (!getBytesMethod || !commitMethod || !addResourceMethod ||
        !addStateMethod)
        return;

    originalGetCommandBytes =
        (GetCommandBytesFn)method_getImplementation(getBytesMethod);
    method_setImplementation(getBytesMethod, (IMP)tracedGetCommandBytes);

    originalCommit = (CommitFn)method_getImplementation(commitMethod);
    const char *commitTypes = method_getTypeEncoding(commitMethod);
    if (!class_addMethod(commandBufferClass, commitSelector,
                         (IMP)tracedCommit, commitTypes))
        method_setImplementation(commitMethod, (IMP)tracedCommit);

    originalAddResourceReference =
        (AddResourceReferenceFn)method_getImplementation(addResourceMethod);
    method_setImplementation(addResourceMethod,
                             (IMP)tracedAddResourceReference);
    originalAddStateReference =
        (AddStateReferenceFn)method_getImplementation(addStateMethod);
    method_setImplementation(addStateMethod, (IMP)tracedAddStateReference);
    hooksInstalled = YES;
    Method blitMethod = class_getInstanceMethod(commandBufferClass, sel_registerName("blitCommandEncoder"));
    if (blitMethod) {
        originalBlitEncoder = (void *)method_getImplementation(blitMethod);
        method_setImplementation(blitMethod, (IMP)tracedBlitEncoder);
    }
    Class texture = objc_getClass("AppleParavirtTexture");
    SEL replace = sel_registerName("replaceRegion:mipmapLevel:withBytes:bytesPerRow:");
    Method replaceMethod = class_getInstanceMethod(texture, replace);
    if (replaceMethod) {
        originalReplace = (void *)method_getImplementation(replaceMethod);
        class_addMethod(texture, replace, (IMP)tracedReplace, method_getTypeEncoding(replaceMethod));
        method_setImplementation(class_getInstanceMethod(texture,replace),(IMP)tracedReplace);
    }
}
