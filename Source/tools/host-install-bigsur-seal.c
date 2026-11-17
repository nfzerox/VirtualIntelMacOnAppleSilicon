#include "host-install-bigsur-seal.h"
#include <AppleArchive/AppleArchive.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int VIMExtractIntelSealArchive(int input, int directory,
    bool (*progress)(uint64_t, void *), void *context) {
    static const char *const names[] = {"digest.db", "mtree.txt"};
    static const AAFieldKey pathKey = {.skey="PAT"}, typeKey = {.skey="TYP"}, dataKey = {.skey="DAT"};
    const size_t capacity = 256U * 1024;
    AAByteStream stream = AAFileStreamOpenWithFD(input, 0);
    AAArchiveStream archive = stream ? AADecodeArchiveInputStreamOpen(stream, NULL, NULL, 0, 0) : NULL;
    AAHeader header = NULL;
    unsigned created = 0;
    bool root = false;
    uint64_t written = 0;
    int output = -1, error = 0;
    uint8_t *buffer = NULL;
    if (!archive) {error = EBADMSG; goto done;}
    if (!(buffer = malloc(capacity))) {error = ENOMEM; goto done;}
    for (;;) {
        if (progress && !progress(written, context)) {error = ECANCELED; goto done;}
        int status = AAArchiveStreamReadHeader(archive, &header);
        if (!status) break;
        uint64_t type, remaining, offset;
        size_t length;
        char path[16];
        if (status < 0 || AAHeaderGetFieldUIntWithKey(header, typeKey, &type) != 1 ||
            AAHeaderGetFieldStringWithKey(header, pathKey, sizeof(path), path, &length) != 1 || strlen(path) != length) {
            error = EBADMSG;
            goto done;
        }
        if (!length && type == 'D' && !root && !created) {root = true; continue;}
        unsigned index;
        if (!strcmp(path, names[0])) index = 0;
        else if (!strcmp(path, names[1])) index = 1;
        else {error = EBADMSG; goto done;}
        if (type != 'F' || (created & (1U << index)) ||
            AAHeaderGetFieldBlobWithKey(header, dataKey, &remaining, &offset) != 1 || offset || !remaining ||
            remaining > UINT64_C(1024) * 1024 * 1024) {error = EBADMSG; goto done;}
        output = openat(directory, names[index], O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (output < 0) {error = errno; goto done;}
        created |= 1U << index;
        while (remaining) {
            if (progress && !progress(written, context)) {error = ECANCELED; goto done;}
            size_t count = remaining < capacity ? (size_t)remaining : capacity;
            if (AAArchiveStreamReadBlob(archive, dataKey, buffer, count)) {error = EBADMSG; goto done;}
            for (size_t sent = 0; sent < count;) {
                ssize_t n = write(output, buffer + sent, count - sent);
                if (n < 0 && errno == EINTR) continue;
                if (n <= 0) {error = n < 0 ? errno : EIO; goto done;}
                sent += (size_t)n;
            }
            remaining -= count;
            written += count;
        }
        if (close(output)) error = errno;
        output = -1;
        if (error) goto done;
    }
    if (created != 3) error = EBADMSG;

done:
    if (output >= 0) close(output);
    if (header) AAHeaderDestroy(header);
    if (AAArchiveStreamClose(archive) && !error) error = EBADMSG;
    if (AAByteStreamClose(stream) && !error) error = EIO;
    free(buffer);
    if (!error) return 0;
    for (unsigned i = 0; i < 2; i++) if (created & (1U << i)) unlinkat(directory, names[i], 0);
    errno = error;
    return -1;
}
