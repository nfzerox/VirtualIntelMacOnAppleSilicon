/*
 * Read-only Apple sparsebundle input for native host installation.
 *
 * The installer closes and detaches its own unencrypted staging image before
 * conversion. Report missing bands as zero extents instead of reading the
 * whole virtual disk through a DiskImages device. Never mount, modify or
 * decrypt the guest filesystem here.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qobject/qdict.h"
#include "qemu/module.h"
#include "qemu/iov.h"
#include "block/block-io.h"
#include "block/block_int.h"
#include <CoreFoundation/CoreFoundation.h>
#include <dirent.h>

typedef struct SparseBundleBand {
    uint64_t number;
    int64_t length;
} SparseBundleBand;

typedef struct SparseBundleState {
    int bands_fd;
    int64_t size, band_size, allocated;
    GArray *bands;
} SparseBundleState;

static void sparsebundle_close(BlockDriverState *bs)
{
    SparseBundleState *s = bs->opaque;

    if (s->bands_fd >= 0) {
        close(s->bands_fd);
        s->bands_fd = -1;
    }
    if (s->bands) {
        g_array_free(s->bands, true);
        s->bands = NULL;
    }
}

static bool sparsebundle_number(CFDictionaryRef info, CFStringRef key,
                                int64_t *value)
{
    CFTypeRef number = CFDictionaryGetValue(info, key);

    return number && CFGetTypeID(number) == CFNumberGetTypeID() &&
           CFNumberGetValue(number, kCFNumberSInt64Type, value);
}

static bool sparsebundle_metadata(int directory, SparseBundleState *s)
{
    struct stat st;
    int fd = openat(directory, "Info.plist", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    g_autofree uint8_t *bytes = NULL;
    CFPropertyListRef info = NULL;
    CFDataRef data = NULL;
    bool good = false;
    size_t done = 0;
    int64_t encryption = 0;

    if (fd < 0) {
        return false;
    }
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) ||
        st.st_size < 1 || st.st_size > 1024 * 1024) {
        goto out;
    }
    bytes = g_malloc(st.st_size);
    while (done < st.st_size) {
        ssize_t n = read(fd, bytes + done, st.st_size - done);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            goto out;
        }
        done += n;
    }
    data = CFDataCreate(NULL, bytes, done);
    if (!data) {
        goto out;
    }
    info = CFPropertyListCreateWithData(NULL, data,
                                       kCFPropertyListImmutable, NULL, NULL);
    if (info && CFGetTypeID(info) == CFDictionaryGetTypeID()) {
        CFTypeRef type = CFDictionaryGetValue(info,
                                   CFSTR("diskimage-bundle-type"));
        CFTypeRef encrypted = CFDictionaryGetValue(info,
                                                  CFSTR("encryption-mode"));
        good = type && CFEqual(type, CFSTR("com.apple.diskimage.sparsebundle")) &&
               sparsebundle_number(info, CFSTR("size"), &s->size) &&
               sparsebundle_number(info, CFSTR("band-size"), &s->band_size) &&
               s->size > 0 && s->size % BDRV_SECTOR_SIZE == 0 &&
               s->band_size >= BDRV_SECTOR_SIZE &&
               s->band_size % BDRV_SECTOR_SIZE == 0 &&
               (!encrypted ||
                (sparsebundle_number(info, CFSTR("encryption-mode"),
                                     &encryption) && !encryption));
    }
out:
    if (info) {
        CFRelease(info);
    }
    if (data) {
        CFRelease(data);
    }
    close(fd);
    return good;
}

static int sparsebundle_order(const void *a, const void *b)
{
    const SparseBundleBand *x = a, *y = b;
    return (x->number > y->number) - (x->number < y->number);
}

static int sparsebundle_open(BlockDriverState *bs, QDict *options, int flags,
                             Error **errp)
{
    SparseBundleState *s = bs->opaque;
    const char *filename = qdict_get_str(options, "filename");
    DIR *entries = NULL;
    int root = -1, enumeration = -1, ret = -EINVAL;

    s->bands_fd = -1;
    if (flags & BDRV_O_RDWR) {
        error_setg(errp, "Apple sparsebundle input is read-only");
        return -EACCES;
    }
    root = open(filename, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (root < 0 || !sparsebundle_metadata(root, s)) {
        error_setg(errp, "Invalid or encrypted Apple sparsebundle metadata");
        goto out;
    }
    s->bands_fd = openat(root, "bands",
                        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (s->bands_fd < 0) {
        error_setg_errno(errp, errno, "Could not open sparsebundle bands");
        goto out;
    }
    enumeration = openat(s->bands_fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    entries = enumeration < 0 ? NULL : fdopendir(enumeration);
    if (!entries) {
        error_setg_errno(errp, errno, "Could not list sparsebundle bands");
        if (enumeration >= 0) {
            close(enumeration);
        }
        goto out;
    }
    s->bands = g_array_new(false, false, sizeof(SparseBundleBand));
    for (;;) {
        struct dirent *entry;
        struct stat st;
        char canonical[32], *end;
        uint64_t number;

        errno = 0;
        entry = readdir(entries);
        if (!entry) {
            if (errno) {
                error_setg_errno(errp, errno, "Could not list sparsebundle bands");
                goto out;
            }
            break;
        }
        if (!*entry->d_name || strspn(entry->d_name, "0123456789abcdef") !=
                              strlen(entry->d_name)) {
            continue;
        }
        errno = 0;
        number = strtoull(entry->d_name, &end, 16);
        if (errno || *end || number > (s->size - 1) / s->band_size) {
            error_setg(errp, "Sparsebundle band is outside the image");
            goto out;
        }
        snprintf(canonical, sizeof(canonical), "%" PRIx64, number);
        if (strcmp(canonical, entry->d_name)) {
            continue; /* DiskImages uses canonical lowercase hexadecimal. */
        }
        if (fstatat(s->bands_fd, canonical, &st, AT_SYMLINK_NOFOLLOW) ||
            !S_ISREG(st.st_mode) || st.st_size < 0 ||
            st.st_size > MIN(s->band_size, s->size - number * s->band_size) ||
            st.st_blocks < 0 || st.st_blocks > (INT64_MAX - s->allocated) / 512) {
            error_setg(errp, "Invalid sparsebundle band file");
            goto out;
        }
        SparseBundleBand band = {number, st.st_size};
        g_array_append_val(s->bands, band);
        s->allocated += st.st_blocks * 512;
    }
    g_array_sort(s->bands, sparsebundle_order);
    qdict_del(options, "filename");
    ret = 0;
out:
    if (entries) {
        closedir(entries);
    }
    if (root >= 0) {
        close(root);
    }
    if (ret) {
        sparsebundle_close(bs);
    }
    return ret;
}

/* First materialized band at or after this position. The index is immutable:
 * only detached input is supported, never a simultaneously writable image. */
static size_t sparsebundle_find(SparseBundleState *s, uint64_t number)
{
    size_t lo = 0, hi = s->bands->len;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (g_array_index(s->bands, SparseBundleBand, mid).number < number) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

static int64_t coroutine_fn sparsebundle_getlength(BlockDriverState *bs)
{
    return ((SparseBundleState *)bs->opaque)->size;
}

static int64_t coroutine_fn sparsebundle_allocated(BlockDriverState *bs)
{
    return ((SparseBundleState *)bs->opaque)->allocated;
}

static int coroutine_fn GRAPH_RDLOCK
sparsebundle_status(BlockDriverState *bs, unsigned int mode,
                    int64_t offset, int64_t bytes, int64_t *pnum, int64_t *map,
                    BlockDriverState **file)
{
    SparseBundleState *s = bs->opaque;
    uint64_t number = offset / s->band_size;
    size_t index = sparsebundle_find(s, number);
    int64_t end = s->size;
    int ret = BDRV_BLOCK_ZERO;

    if (index < s->bands->len) {
        SparseBundleBand *band = &g_array_index(s->bands, SparseBundleBand, index);
        int64_t base = band->number * s->band_size;
        if (band->number == number && offset < base + band->length) {
            /* Preserve a partially written final sector too. */
            end = MIN(s->size, base + ROUND_UP(band->length, BDRV_SECTOR_SIZE));
            ret = BDRV_BLOCK_DATA;
        } else {
            if (band->number == number) {
                ++index;
            }
            if (index < s->bands->len) {
                end = g_array_index(s->bands, SparseBundleBand, index).number *
                      s->band_size;
            }
        }
    }
    *pnum = MIN(bytes, end - offset);
    *map = 0;
    *file = NULL;
    return ret;
}

static int sparsebundle_read_band(int fd, int64_t offset, QEMUIOVector *qiov,
                                  size_t at, size_t bytes)
{
    QEMUIOVector slice;
    int ret = 0;
    qemu_iovec_init_slice(&slice, qiov, at, bytes);
    for (int i = 0; !ret && i < slice.niov; ++i) {
        uint8_t *p = slice.iov[i].iov_base;
        size_t left = slice.iov[i].iov_len;
        while (left) {
            ssize_t n = pread(fd, p, left, offset);
            if (n < 0 && errno == EINTR) {
                continue;
            }
            if (n <= 0) {
                ret = n < 0 ? -errno : -EIO;
                break;
            }
            p += n;
            left -= n;
            offset += n;
        }
    }
    qemu_iovec_destroy(&slice);
    return ret;
}

static int coroutine_fn GRAPH_RDLOCK
sparsebundle_preadv(BlockDriverState *bs, int64_t offset, int64_t bytes,
                   QEMUIOVector *qiov, BdrvRequestFlags flags)
{
    SparseBundleState *s = bs->opaque;
    size_t done = 0;
    while (bytes) {
        uint64_t number = offset / s->band_size;
        size_t index = sparsebundle_find(s, number);
        SparseBundleBand *band = index < s->bands->len ?
            &g_array_index(s->bands, SparseBundleBand, index) : NULL;
        int64_t start = number * s->band_size;
        int64_t span = MIN(bytes, s->band_size - (offset - start));
        if (band && band->number == number && offset - start < band->length) {
            char name[32];
            struct stat st;
            snprintf(name, sizeof(name), "%" PRIx64, number);
            int fd = openat(s->bands_fd, name,
                            O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
            if (fd < 0) {
                return -errno;
            }
            if (fstat(fd, &st) || !S_ISREG(st.st_mode)) {
                close(fd);
                return -EIO;
            }
            span = MIN(span, band->length - (offset - start));
            int ret = sparsebundle_read_band(fd, offset - start, qiov, done, span);
            close(fd);
            if (ret) {
                return ret;
            }
        } else {
            qemu_iovec_memset(qiov, done, 0, span);
        }
        offset += span;
        bytes -= span;
        done += span;
    }
    return 0;
}

static void sparsebundle_parse_filename(const char *filename, QDict *options,
                                        Error **errp)
{
    bdrv_parse_filename_strip_prefix(filename, "apple-sparsebundle:", options);
}

static BlockDriver bdrv_apple_sparsebundle = {
    .format_name = "apple-sparsebundle",
    .protocol_name = "apple-sparsebundle",
    .instance_size = sizeof(SparseBundleState),
    .bdrv_needs_filename = true,
    .bdrv_open = sparsebundle_open,
    .bdrv_close = sparsebundle_close,
    .bdrv_parse_filename = sparsebundle_parse_filename,
    .bdrv_co_getlength = sparsebundle_getlength,
    .bdrv_co_get_allocated_file_size = sparsebundle_allocated,
    .bdrv_co_block_status = sparsebundle_status,
    .bdrv_co_preadv = sparsebundle_preadv,
};

static void sparsebundle_init(void)
{
    bdrv_register(&bdrv_apple_sparsebundle);
}
block_init(sparsebundle_init);
