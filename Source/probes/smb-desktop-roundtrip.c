// Finite host-side SMB copy test. Use an already authenticated home mount;
// never read credentials, change sharing policy, or touch existing guest files.
#include <copyfile.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

static unsigned char data[65536];
static const unsigned char resource[] = {0, 1, 2, 3, 0xff, 0xfe, 0xfd, 0xfc};
static const char attribute[] = "org.virtualintelmac.smb-copy-test";
static const char value[] = "Finite metadata round trip";

static int verify(const char *path) {
    unsigned char buffer[sizeof(data)];
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    size_t count = 0;
    while (count < sizeof(buffer)) {
        ssize_t n = read(fd, buffer + count, sizeof(buffer) - count);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { close(fd); errno = EIO; return -1; }
        count += (size_t)n;
    }
    unsigned char extra;
    int result = read(fd, &extra, 1) == 0 && !memcmp(buffer, data, sizeof(data));
    char metadata[128];
    ssize_t n = fgetxattr(fd, attribute, metadata, sizeof(metadata), 0, 0);
    result &= n == (ssize_t)sizeof(value) && !memcmp(metadata, value, sizeof(value));
    n = fgetxattr(fd, "com.apple.ResourceFork", metadata, sizeof(metadata), 0, 0);
    result &= n == (ssize_t)sizeof(resource) && !memcmp(metadata, resource, sizeof(resource));
    close(fd);
    if (!result) errno = EIO;
    return result ? 0 : -1;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fputs("usage: smb-desktop-roundtrip MOUNTED_HOME/Desktop\n", stderr);
        return 64;
    }
    struct statfs fs;
    if (statfs(argv[1], &fs) || strcmp(fs.f_fstypename, "smbfs")) {
        fputs("The target must be an already mounted SMB Desktop directory.\n", stderr);
        return 64;
    }
    char local[] = "/private/tmp/vim-smb-source.XXXXXX";
    char remote[PATH_MAX], source[PATH_MAX], back[PATH_MAX], copied[PATH_MAX];
    if (snprintf(remote, sizeof(remote), "%s/VIM-SMB-Copy.XXXXXX", argv[1]) >= (int)sizeof(remote)) return 64;
    if (!mkdtemp(local)) { perror("local fixture directory"); return 1; }
    if (!mkdtemp(remote)) { perror("SMB fixture directory"); rmdir(local); return 1; }
    printf("Private fixture directories: %s and %s\n", local, remote);
    snprintf(source, sizeof(source), "%s/source", local);
    snprintf(back, sizeof(back), "%s/returned", local);
    for (size_t i = 0; i < sizeof(data); ++i) data[i] = (unsigned char)(i * 31 + (i >> 8));
    int fd = open(source, O_CREAT | O_EXCL | O_WRONLY, 0600);
    if (fd < 0) goto failed;
    size_t count = 0;
    while (count < sizeof(data)) {
        ssize_t n = write(fd, data + count, sizeof(data) - count);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { close(fd); goto failed; }
        count += (size_t)n;
    }
    int result = fsetxattr(fd, attribute, value, sizeof(value), 0, 0);
    if (!result) result = fsetxattr(fd, "com.apple.ResourceFork", resource, sizeof(resource), 0, 0);
    close(fd);
    if (result || verify(source)) goto failed;
    const char *names[] = {"Plain.png", "Screen Shot – 12.37.00 PM.png", "Café.png"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (snprintf(copied, sizeof(copied), "%s/%s", remote, names[i]) >= (int)sizeof(copied)) goto failed;
        if (copyfile(source, copied, NULL, COPYFILE_ALL | COPYFILE_EXCL) || verify(copied) ||
            copyfile(copied, back, NULL, COPYFILE_ALL | COPYFILE_EXCL) || verify(back)) goto failed;
        if (unlink(back) || unlink(copied)) goto failed;
        printf("PASS: name variant %zu, 65536 data bytes, extended attribute and resource fork both ways\n", i + 1);
    }
    if (unlink(source) || rmdir(local) || rmdir(remote)) goto failed;
    return 0;
failed:
    perror("SMB round trip failed; unique fixture directories retained for diagnosis");
    return 1;
}
