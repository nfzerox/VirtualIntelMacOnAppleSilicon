#include "VIMPackagePublication.h"
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

int VIMPublishPreparedPackage(const char *prepared, const char *destination) {
    struct stat source;
    if (lstat(prepared, &source)) return -1;
    if (!S_ISDIR(source.st_mode)) {errno = ENOTDIR; return -1;}
    if (!renamex_np(prepared, destination, RENAME_EXCL)) return 0;
    if (errno != ENOTSUP && errno != EOPNOTSUPP) return -1;

    // ExFAT does not support RENAME_EXCL, but mkdir is exclusive even
    // there. Rename over only our empty reservation. In particular, never
    // fall back to renaming over a path that already existed.
    if (mkdir(destination, 0700)) return -1;
    struct stat reservation;
    if (lstat(destination, &reservation)) return -1;
    if (!S_ISDIR(reservation.st_mode)) {errno = EEXIST; return -1;}
    if (!rename(prepared, destination)) return 0;
    int error = errno;
    // Clean up only our empty reservation; never delete package contents.
    struct stat current;
    if (!lstat(destination, &current) && S_ISDIR(current.st_mode) &&
        current.st_dev == reservation.st_dev && current.st_ino == reservation.st_ino)
        rmdir(destination);
    errno = error;
    return -1;
}
