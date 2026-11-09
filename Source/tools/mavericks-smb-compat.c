// Mavericks smbd advertises AAPL Resolve ID, but a modern SMB client can
// invalidate the session's file IDs on create. Finder caches the volume's
// original capability and then fails to resolve directories (error -43).
// Stop advertising just Resolve ID; leave SMB2, signing and auth unchanged.
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <unistd.h>

static const char smbd_path[] = "/usr/sbin/smbd";
// darwin_tree::bind: (format capabilities & valid) -> PATH_FROM_ID bool.
// Both AAPL's capability reply and the resolver consult this one bool.
static const unsigned char original[] = {
    0x89,0xc1,0x81,0xe1,0,0x40,0,0,0xc1,0xe9,0x0e,0x88,0x4b,0x2a,
    0xc6,0x43,0x2b,1,0xc6,0x43,0x2c,1
};

extern char **environ;
static int run(char *const argv[])
{
    pid_t child;
    int status;
    if (posix_spawn(&child, argv[0], NULL, NULL, argv, environ)) return -1;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static bool transfer(int fd, void *bytes, size_t remaining, bool writing)
{
    unsigned char *cursor = bytes;
    while (remaining) {
        ssize_t n = writing ? write(fd, cursor, remaining) : read(fd, cursor, remaining);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        cursor += n;
        remaining -= (size_t)n;
    }
    return true;
}

// A fresh guest has no developer tools, so /usr/bin/codesign cannot find
// its codesign_allocate helper. Use Apple's allocator staged beside this tool,
// for our own signer children only, as guest preparation does.
static void use_staged_allocator(const char *self)
{
    if (getenv("CODESIGN_ALLOCATE") || !self || self[0] != '/') return;
    const char *slash = strrchr(self, '/');
    char allocator[1024];
    if ((size_t)(slash - self) + sizeof("/codesign_allocate") > sizeof(allocator)) return;
    snprintf(allocator, sizeof(allocator), "%.*s/codesign_allocate", (int)(slash - self), self);
    if (!access(allocator, X_OK)) setenv("CODESIGN_ALLOCATE", allocator, 1);
}

int main(int argc, char **argv)
{
    // Do not patch the recovery environment or any newer guest, even if its
    // compiler emitted an identical instruction sequence.
    char release[32] = {0};
    size_t release_size = sizeof(release);
    if (sysctlbyname("kern.osrelease", release, &release_size, NULL, 0) ||
        strncmp(release, "13.", 3) || access("/var/db/.AppleSetupDone", F_OK)) return 0;
    if (geteuid()) return 1;
    use_staged_allocator(argc > 0 ? argv[0] : NULL);

    int fd = open(smbd_path, O_RDONLY | O_NOFOLLOW);
    struct stat st;
    if (fd < 0 || flock(fd, LOCK_EX | LOCK_NB) || fstat(fd, &st) ||
        !S_ISREG(st.st_mode) || st.st_uid || (st.st_mode & 022) ||
        st.st_size < 4096 || st.st_size > 4 * 1024 * 1024) return 1;
    size_t size = (size_t)st.st_size;
    unsigned char *image = malloc(size);
    if (!image || !transfer(fd, image, size, false)) return 1;
    // Require the native x86_64 executable, not a fat or unrelated object.
    if (memcmp(image, "\xcf\xfa\xed\xfe\x07\x00\x00\x01", 8)) return 0;
    unsigned char patched[sizeof(original)];
    memcpy(patched, original, sizeof(patched));
    patched[5] = 0;
    unsigned char *match = NULL;
    for (size_t i = 0; i <= size - sizeof(original); ++i) {
        if (!memcmp(image + i, patched, sizeof(patched))) return 0;
        if (!memcmp(image + i, original, sizeof(original))) {
            if (match) return 0;
            match = image + i;
        }
    }
    if (!match) return 0; // Unknown build: leave the system's daemon intact.
    match[5] = 0;

    char temp[] = "/usr/sbin/.vim-smbd.XXXXXX";
    int candidate = mkstemp(temp);
    if (candidate < 0) return 1;
    int result = 1;
    bool written = transfer(candidate, image, size, true) &&
                   !fchown(candidate, st.st_uid, st.st_gid) &&
                   !fchmod(candidate, st.st_mode & 0777);
    close(candidate);
    if (!written ||
        run((char *const[]){"/usr/bin/codesign","--force","--sign","-",
            "--preserve-metadata=identifier,entitlements,flags",temp,NULL}) ||
        run((char *const[]){"/usr/bin/codesign","--verify",temp,NULL})) goto done;

    // Preserve the exact Apple-signed original. The active daemon is replaced
    // atomically only after the replacement has been signed and verified.
    char backup[sizeof(temp) + sizeof(".original")];
    snprintf(backup, sizeof(backup), "%s.original", temp);
    struct stat live;
    if (lstat(smbd_path, &live) || live.st_ino != st.st_ino || live.st_dev != st.st_dev ||
        link(smbd_path, backup)) goto done;
    if (rename(temp, smbd_path)) {
        unlink(backup);
        goto done;
    }
    // No launchd configuration change: disabled File Sharing stays disabled.
    // Restart an already running listener once so new connections use the fix.
    if (!run((char *const[]){"/bin/launchctl","list","com.apple.smbd",NULL}) &&
        run((char *const[]){"/bin/launchctl","stop","com.apple.smbd",NULL})) {
        if (rename(backup, smbd_path)) fprintf(stderr, "Original SMB daemon: %s\n", backup);
        goto done;
    }
    result = 0;
done:
    unlink(temp);
    close(fd);
    free(image);
    return result;
}
