#include <dlfcn.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <unistd.h>

typedef void *(*CreateFn)(void);
typedef void (*DestroyFn)(void *);
typedef int (*SetPluginPathFn)(void *, const char *, const void *, size_t);
typedef void (^ReplyBlock)(uint32_t, const void *, size_t, const char *);
typedef void (*BuildRequestFn)(void *, uint32_t, uint32_t, const void *,
                               size_t, ReplyBlock);

static void *realImage;
static CreateFn realCreate;
static DestroyFn realDestroy;
static SetPluginPathFn realSetPluginPath;
static BuildRequestFn realBuildRequest;
static uint32_t requestSequence;

static uint32_t readU32(const uint8_t *bytes, size_t offset) {
    uint32_t value;
    __builtin_memcpy(&value, bytes + offset, sizeof(value));
    return value;
}

static void writeU32(uint8_t *bytes, size_t offset, uint32_t value) {
    __builtin_memcpy(bytes + offset, &value, sizeof(value));
}

static void shiftPipelineOffset(uint8_t *bytes, size_t offset,
                                uint32_t oldHeaderSize,
                                uint32_t headerGrowth) {
    uint32_t value = readU32(bytes, offset);
    if (value >= oldHeaderSize)
        writeU32(bytes, offset, value + headerGrowth);
}

static int loadRealCompiler(void) {
    if (realImage)
        return realCreate && realDestroy && realSetPluginPath &&
               realBuildRequest;
    realImage = dlopen(
        "/System/Library/PrivateFrameworks/MTLCompiler.framework/Versions/"
        "31001/MTLCompilerReal",
        RTLD_NOW | RTLD_LOCAL);
    if (!realImage)
        return 0;
    realCreate = (CreateFn)dlsym(realImage, "MTLCodeGenServiceCreate");
    realDestroy = (DestroyFn)dlsym(realImage, "MTLCodeGenServiceDestroy");
    realSetPluginPath =
        (SetPluginPathFn)dlsym(realImage, "MTLCodeGenServiceSetPluginPath");
    realBuildRequest =
        (BuildRequestFn)dlsym(realImage, "MTLCodeGenServiceBuildRequest");
    return realCreate && realDestroy && realSetPluginPath && realBuildRequest;
}

void *MTLCodeGenServiceCreate(void) {
    return loadRealCompiler() ? realCreate() : NULL;
}

void MTLCodeGenServiceDestroy(void *compiler) {
    if (loadRealCompiler())
        realDestroy(compiler);
}

int MTLCodeGenServiceSetPluginPath(void *compiler, const char *path,
                                   const void *data, size_t size) {
    return loadRealCompiler()
               ? realSetPluginPath(compiler, path, data, size)
               : -1;
}

void MTLCodeGenServiceBuildRequest(void *compiler, uint32_t version,
                                   uint32_t requestType, const void *request,
                                   size_t requestSize, void *reply) {
#if PVG_TRACE_COMPILER_REQUESTS
    uint32_t sequence = __sync_add_and_fetch(&requestSequence, 1);
    char path[128];
    snprintf(path, sizeof(path), "/tmp/pvg-mtl-request-%d-%u-v%u-t%u.bin",
             getpid(), sequence, version, requestType);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) {
        if (request && requestSize)
            (void)write(fd, request, requestSize);
        close(fd);
    }
    const uint8_t *bytes = (const uint8_t *)request;
    size_t traceSize = requestSize < 512 ? requestSize : 512;
    fprintf(stderr, "PVGMTL request n=%u version=%u type=%u size=%zu\n",
            sequence, version, requestType, requestSize);
    syslog(LOG_NOTICE, "PVGMTL request n=%u version=%u type=%u size=%zu",
           sequence, version, requestType, requestSize);
    for (size_t offset = 0; offset < traceSize; offset += 16) {
        char line[80];
        int length = snprintf(line, sizeof(line), "PVGMTL %04zx", offset);
        fprintf(stderr, "PVGMTL %04zx", offset);
        size_t lineEnd = offset + 16 < traceSize ? offset + 16 : traceSize;
        for (size_t index = offset; index < lineEnd; ++index) {
            fprintf(stderr, " %02x", bytes[index]);
            if (length > 0 && (size_t)length < sizeof(line))
                length += snprintf(line + length, sizeof(line) - length,
                                   " %02x", bytes[index]);
        }
        fputc('\n', stderr);
        syslog(LOG_NOTICE, "%s", line);
    }
    fflush(stderr);
#endif
    const void *adaptedRequest = request;
    size_t adaptedSize = requestSize;
    void *ownedRequest = NULL;

    /* Catalina's source request has a 12-byte header:
     *
     *   +0 sourceSize, +4 compilerOptionsSize, +8 flags
     *
     * Big Sur inserted binaryDataSize at +4 and moved the latter two fields:
     *
     *   +0 sourceSize, +4 binaryDataSize, +8 compilerOptionsSize, +12 flags
     *
     * Catalina never sends the intervening dispatch-data array, so translating
     * its request only requires inserting a zero word at +4.  The source and
     * compiler-options payload then retain the alignment expected by 31001.
     */
    if (requestType == 3 && request && requestSize >= 12) {
        uint8_t *bytes = (uint8_t *)malloc(requestSize + 4);
        if (bytes) {
            __builtin_memcpy(bytes, request, 4);
            *(uint32_t *)(bytes + 4) = 0;
            __builtin_memcpy(bytes + 8, (const uint8_t *)request + 4,
                             requestSize - 4);
            adaptedRequest = bytes;
            adaptedSize = requestSize + 4;
            ownedRequest = bytes;
        }
    } else if ((requestType == 1 || requestType == 6) && request &&
               requestSize >= 0x70) {
        enum {
            CatalinaPipelineHeaderSize = 0x70,
            BigSurPipelineHeaderSize = 0x98,
            PipelineHeaderGrowth =
                BigSurPipelineHeaderSize - CatalinaPipelineHeaderSize
        };
        uint8_t *bytes = (uint8_t *)calloc(1, requestSize +
                                                 PipelineHeaderGrowth);
        if (bytes) {
            __builtin_memcpy(bytes, request, CatalinaPipelineHeaderSize);
            __builtin_memcpy(bytes + BigSurPipelineHeaderSize,
                             (const uint8_t *)request +
                                 CatalinaPipelineHeaderSize,
                             requestSize - CatalinaPipelineHeaderSize);

            /* These Catalina header members are offsets into the contiguous
             * request.  Account for the appended Big Sur header fields while
             * leaving scalar sizes and flags untouched. */
            static const uint8_t offsetFields[] = {
                0x08, 0x10, 0x18, 0x20, 0x58, 0x60, 0x68
            };
            for (size_t index = 0;
                 index < sizeof(offsetFields) / sizeof(offsetFields[0]);
                 ++index)
                shiftPipelineOffset(bytes, offsetFields[index],
                                    CatalinaPipelineHeaderSize,
                                    PipelineHeaderGrowth);

            /* Catalina has no visible-function group array.  Its equivalent
             * end cursor becomes the start/end of Big Sur's empty array. */
            writeU32(bytes, 0x74,
                     (uint32_t)(requestSize + PipelineHeaderGrowth));
            writeU32(bytes, 0x90, requestType);
            writeU32(bytes, 0x94, 1); /* PLATFORM_MACOS */

            adaptedRequest = bytes;
            adaptedSize = requestSize + PipelineHeaderGrowth;
            ownedRequest = bytes;
        }
    }

    if (loadRealCompiler()) {
#if PVG_TRACE_COMPILER_REQUESTS
        ReplyBlock originalReply = (ReplyBlock)reply;
        ReplyBlock tracedReply = ^(uint32_t status, const void *data,
                                   size_t size, const char *error) {
            const uint8_t *replyBytes = (const uint8_t *)data;
            size_t traceReplySize = size < 512 ? size : 512;
            syslog(LOG_NOTICE,
                   "PVGMTL reply type=%u status=%u size=%zu error=%s",
                   requestType, status, size, error ? error : "(null)");
            for (size_t offset = 0; offset < traceReplySize; offset += 16) {
                char line[80];
                int length = snprintf(line, sizeof(line),
                                      "PVGMTL reply %04zx", offset);
                size_t lineEnd = offset + 16 < traceReplySize
                                     ? offset + 16
                                     : traceReplySize;
                for (size_t index = offset; index < lineEnd; ++index) {
                    if (length > 0 && (size_t)length < sizeof(line))
                        length += snprintf(line + length,
                                           sizeof(line) - length,
                                           " %02x", replyBytes[index]);
                }
                syslog(LOG_NOTICE, "%s", line);
            }
            originalReply(status, data, size, error);
        };
        realBuildRequest(compiler, version, requestType, adaptedRequest,
                         adaptedSize, tracedReply);
#else
        realBuildRequest(compiler, version, requestType, adaptedRequest,
                         adaptedSize, (ReplyBlock)reply);
#endif
    }
    free(ownedRequest);
}
