// Read-only native inode-to-path diagnostic; no recursive filesystem walk.
#include <sys/mount.h>
#include <sys/fsgetpath.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc != 2) return 64;
    char *end;
    uint64_t fileid = strtoull(argv[1], &end, 0);
    if (!*argv[1] || *end) return 64;
    struct statfs *mounts;
    int count = getmntinfo(&mounts, MNT_NOWAIT);
    if (!count) { perror("getmntinfo"); return 1; }
    for (int i = 0; i < count; i++) {
        if (strcmp(mounts[i].f_fstypename, "apfs")) continue;
        char path[PATH_MAX];
        ssize_t size = fsgetpath(path, sizeof(path), &mounts[i].f_fsid, fileid);
        if (size > 0)
            printf("%s on %s: inode=%" PRIu64 " path=%s\n",
                mounts[i].f_mntfromname, mounts[i].f_mntonname, fileid, path);
    }
    return 0;
}
