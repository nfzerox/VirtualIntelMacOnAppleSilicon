#include "host-install-pbzx.h"
#include <compression.h>
#include <dispatch/dispatch.h>
#include <errno.h>
#include <lzma.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint64_t ReadBigEndian(const uint8_t *bytes) {
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; i++) value = (value << 8) | bytes[i];
    return value;
}

// Clean EOF is distinct from a truncated header or payload.
static int ReadInput(VIMArchiveRead read, void *context, uint8_t *bytes, size_t length) {
    size_t done = 0;
    while (done < length) {
        ssize_t n = read(context, bytes + done, length - done);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) return -1;
        if (!n) {
            if (!done) return 0;
            errno = EBADMSG;
            return -1;
        }
        done += (size_t)n;
    }
    return 1;
}

static ssize_t ReadFD(void *context, void *bytes, size_t length) {return read(*(int *)context, bytes, length);}
static int ReadBytes(int fd, uint8_t *bytes, size_t length) {return ReadInput(ReadFD, &fd, bytes, length);}

static int WritePayload(int output, const uint8_t *payload, size_t length,
    uint64_t consumed, uint64_t *produced, VIMArchiveProgress progress, void *context) {
    for (size_t offset = 0; offset < length;) {
        if (progress && !progress(consumed, *produced, context)) {errno = ECANCELED; return -1;}
        size_t count = length - offset;
        if (count > 256U * 1024) count = 256U * 1024;
        ssize_t n = write(output, payload + offset, count);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // A backpressured nonblocking consumer must not hide Pause or
            // Cancel while it works. The next iteration checks progress
            // again before attempting to write the same unsent bytes.
            struct pollfd ready = { .fd = output, .events = POLLOUT };
            if (poll(&ready, 1, 250) < 0 && errno != EINTR) return -1;
            continue;
        }
        if (n <= 0) {if (!n) errno = EIO; return -1;}
        offset += (size_t)n;
        *produced += (size_t)n;
    }
    return 0;
}

typedef struct {
    uint8_t *stored, *decoded;
    size_t length, size;
    uint64_t consumed;
    int error;
    bool active;
    dispatch_semaphore_t ready;
} PBZXJob;

static void DecodePBZXJob(void *opaque) {
    static const uint8_t xzMagic[] = {0xfd, '7', 'z', 'X', 'Z', 0};
    PBZXJob *job = opaque;
    job->error = 0;
    if (job->size >= sizeof(xzMagic) && !memcmp(job->stored, xzMagic, sizeof(xzMagic))) {
        uint64_t memory = 128U * 1024 * 1024;
        size_t in = 0, out = 0;
        lzma_ret status = lzma_stream_buffer_decode(&memory, LZMA_CONCATENATED, NULL,
            job->stored, &in, job->size, job->decoded, &out, job->length);
        if (status == LZMA_MEM_ERROR || status == LZMA_MEMLIMIT_ERROR) job->error = ENOMEM;
        else if (status != LZMA_OK || in != job->size || out != job->length) job->error = EBADMSG;
    } else if (job->size != job->length) job->error = EBADMSG;
    dispatch_semaphore_signal(job->ready);
}

int VIMDecodePBZXFromReader(VIMArchiveRead read, void *input, int output, VIMArchiveProgress progress, void *context) {
    // Two independent blocks overlap XZ decoding with the ordered archive
    // consumer. Keep input reads and progress callbacks on the calling thread.
    enum { Jobs = 2 };
    PBZXJob jobs[Jobs] = {0};
    uint8_t header[16];
    uint64_t consumed = 0, reported = 0, produced = 0;
    unsigned head = 0, pending = 0;
    bool ended = false;
    int error = EBADMSG;
    int result = ReadInput(read, input, header, 12);
    if (result < 0) return -1;
    if (!result || memcmp(header, "pbzx", 4)) goto fail;
    uint64_t maximum = ReadBigEndian(header + 4);
    if (!maximum || maximum > 16U * 1024 * 1024) goto fail;
    consumed = reported = 12;
    for (unsigned i = 0; i < Jobs; i++) jobs[i].ready = dispatch_semaphore_create(0);
    while (!ended || pending) {
        while (!ended && pending < Jobs) {
            if (progress && !progress(reported, produced, context)) {error = ECANCELED; goto fail;}
            result = ReadInput(read, input, header, sizeof(header));
            if (result < 0) {error = errno; goto fail;}
            if (!result) {ended = true; break;}
            uint64_t length = ReadBigEndian(header), size = ReadBigEndian(header + 8);
            if (!size || size > length || length > maximum) goto fail;
            PBZXJob *job = &jobs[(head + pending) % Jobs];
            if (!job->stored && !(job->stored = malloc((size_t)maximum))) {error = ENOMEM; goto fail;}
            // A reused slot can have a longer decoded block than before.
            // Reserve maximum once instead of reallocating an active buffer.
            if (!job->decoded && !(job->decoded = malloc((size_t)maximum))) {error = ENOMEM; goto fail;}
            result = ReadInput(read, input, job->stored, (size_t)size);
            if (result < 0) {error = errno; goto fail;}
            if (!result) goto fail;
            if (consumed > UINT64_MAX - sizeof(header) - size) {error = EOVERFLOW; goto fail;}
            consumed += sizeof(header) + size;
            job->length = (size_t)length; job->size = (size_t)size; job->consumed = consumed;
            job->active = true;
            dispatch_async_f(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), job, DecodePBZXJob);
            pending++;
        }
        if (!pending) break;
        PBZXJob *job = &jobs[head];
        dispatch_semaphore_wait(job->ready, DISPATCH_TIME_FOREVER); job->active = false;
        if (job->error) {error = job->error; goto fail;}
        if (produced > UINT64_MAX - job->length) {error = EOVERFLOW; goto fail;}
        reported = job->consumed;
        const uint8_t *payload = job->size >= 6 && !memcmp(job->stored, "\xfd" "7zXZ\0", 6) ? job->decoded : job->stored;
        if (WritePayload(output, payload, job->length, reported, &produced, progress, context)) {
            error = errno;
            goto fail;
        }
        head = (head + 1) % Jobs; pending--;
    }
    if (progress && !progress(consumed, produced, context)) {error = ECANCELED; goto fail;}
    error = 0;
fail:
    // Never release a queued decoder's buffers while it is still running.
    for (unsigned i = 0; i < Jobs; i++) {
        if (jobs[i].active) dispatch_semaphore_wait(jobs[i].ready, DISPATCH_TIME_FOREVER);
        free(jobs[i].decoded); free(jobs[i].stored);
        if (jobs[i].ready) dispatch_release(jobs[i].ready);
    }
    if (!error) return 0;
    errno = error;
    return -1;
}

int VIMDecodePBZX(int input, int output, VIMArchiveProgress progress, void *context) {
    return VIMDecodePBZXFromReader(ReadFD, &input, output, progress, context);
}

static int ReadBounded(int fd, uint8_t *bytes, size_t size, uint64_t *remaining) {
    if (size > *remaining) {errno = EBADMSG; return -1;}
    int result = ReadBytes(fd, bytes, size);
    if (result <= 0) {if (!result) errno = EBADMSG; return -1;}
    *remaining -= size;
    return 0;
}

static int ReadDERLength(int input, uint8_t tag, uint64_t *length, uint64_t *remaining) {
    uint8_t head[4];
    if (ReadBounded(input, head, 2, remaining)) return -1;
    if (head[0] != tag) {errno = EBADMSG; return -1;}
    *length = head[1];
    if (*length & 0x80) {
        unsigned width = head[1] & 0x7f;
        if (!width || width > sizeof(head)) {errno = EBADMSG; return -1;}
        if (ReadBounded(input, head, width, remaining)) return -1;
        *length = 0;
        for (unsigned i = 0; i < width; i++) *length = (*length << 8) | head[i];
        if (!head[0] || *length < 128) {errno = EBADMSG; return -1;}
    }
    if (*length > *remaining) {errno = EBADMSG; return -1;}
    return 0;
}

static int RequireIA5(int input, const char expected[4], uint64_t *remaining) {
    uint64_t length;
    uint8_t value[4];
    if (ReadDERLength(input, 0x16, &length, remaining)) return -1;
    if (length != sizeof(value)) {errno = EBADMSG; return -1;}
    if (ReadBounded(input, value, sizeof(value), remaining)) return -1;
    if (memcmp(value, expected, sizeof(value))) {errno = EBADMSG; return -1;}
    return 0;
}

static bool DecodeLZFSE(const uint8_t *stored, size_t size, uint8_t *decoded, size_t length) {
    // The system stream may consume bytes after its terminator. Require the
    // block to end with LZFSE's end marker as well as reaching STATUS_END.
    if (size < 4 || memcmp(stored + size - 4, "bvx$", 4)) return false;
    compression_stream stream = {0};
    if (compression_stream_init(&stream, COMPRESSION_STREAM_DECODE, COMPRESSION_LZFSE) != COMPRESSION_STATUS_OK)
        return false;
    stream.src_ptr = stored;
    stream.src_size = size;
    stream.dst_ptr = decoded;
    stream.dst_size = length + 1;
    compression_status status = compression_stream_process(&stream, COMPRESSION_STREAM_FINALIZE);
    bool complete = status == COMPRESSION_STATUS_END && !stream.src_size && stream.dst_size == 1;
    compression_stream_destroy(&stream);
    return complete;
}

int VIMDecodeIntelMTree(int input, int output, VIMArchiveProgress progress, void *context) {
    uint8_t head[256], *stored = NULL, *decoded = NULL;
    const uint64_t maxContainer = (uint64_t)UINT32_MAX + 6;
    uint64_t budget = maxContainer, remaining, length, produced = 0;
    int error = EBADMSG;
    if (ReadDERLength(input, 0x30, &remaining, &budget)) return -1;
    uint64_t total = remaining + (maxContainer - budget);
    if (RequireIA5(input, "IM4P", &remaining) || RequireIA5(input, "xmtr", &remaining) ||
        ReadDERLength(input, 0x16, &length, &remaining)) return -1;
    while (length) {
        size_t count = length < sizeof(head) ? (size_t)length : sizeof(head);
        if (ReadBounded(input, head, count, &remaining)) return -1;
        length -= count;
    }
    if (ReadDERLength(input, 4, &length, &remaining)) return -1;
    if (length != remaining || length < 28) {errno = EBADMSG; return -1;}
    if (ReadBounded(input, head, 12, &remaining)) return -1;
    uint64_t maximum = ReadBigEndian(head + 4);
    if (memcmp(head, "pbze", 4) || !maximum || maximum > 16U * 1024 * 1024) {
        errno = EBADMSG;
        return -1;
    }
    while (remaining) {
        if (progress && !progress(total - remaining, produced, context)) {error = ECANCELED; goto fail;}
        if (ReadBounded(input, head, 16, &remaining)) {error = errno; goto fail;}
        length = ReadBigEndian(head);
        uint64_t size = ReadBigEndian(head + 8);
        if (!size || size > length || length > maximum || size > remaining) goto fail;
        if (!stored && !(stored = malloc((size_t)maximum))) {error = ENOMEM; goto fail;}
        if (ReadBounded(input, stored, (size_t)size, &remaining)) {error = errno; goto fail;}
        const uint8_t *payload = stored;
        if (size != length) {
            if (!decoded && !(decoded = malloc((size_t)maximum + 1))) {error = ENOMEM; goto fail;}
            if (!DecodeLZFSE(stored, (size_t)size, decoded, (size_t)length)) goto fail;
            payload = decoded;
        }
        if (WritePayload(output, payload, (size_t)length, total - remaining, &produced, progress, context)) {
            error = errno;
            goto fail;
        }
    }
    int trailing = ReadBytes(input, head, 1);
    if (trailing < 0) {error = errno; goto fail;}
    if (trailing) goto fail;
    if (progress && !progress(total, produced, context)) {error = ECANCELED; goto fail;}
    free(decoded);
    free(stored);
    return 0;

fail:
    free(decoded);
    free(stored);
    errno = error;
    return -1;
}

#ifdef VIM_PBZX_CLI
#include "host-install-bigsur-seal.h"
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    if (argc == 3 && !strcmp(argv[1], "--extract-intel-seal")) {
        int directory = open(argv[2], O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (directory < 0) {perror("Intel seal directory"); return 1;}
        int status = VIMExtractIntelSealArchive(STDIN_FILENO, directory, NULL, NULL);
        if (status) perror("Intel seal archive");
        close(directory);
        return status ? 1 : 0;
    }
    bool mtree = argc == 2 && !strcmp(argv[1], "--intel-mtree");
    if (argc != 1 && !mtree) {fputs("Usage: host-install-pbzx [--intel-mtree | --extract-intel-seal DIRECTORY]\n", stderr); return 2;}
    int status = mtree ? VIMDecodeIntelMTree(STDIN_FILENO, STDOUT_FILENO, NULL, NULL) :
                        VIMDecodePBZX(STDIN_FILENO, STDOUT_FILENO, NULL, NULL);
    if (!status) return 0;
    perror(mtree ? "Intel Merkle archive" : "PBZX");
    return 1;
}
#endif
