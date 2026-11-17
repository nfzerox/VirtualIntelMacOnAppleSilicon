/* Read-only diagnostic for a guest whose stock command binaries cannot start.
 * Respects the caller's identity and ordinary filesystem permissions.
 * Usage: native-file-read --identity | --list DIRECTORY | --read FILE
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--identity")) {
        printf("uid=%u euid=%u gid=%u\n", getuid(), geteuid(), getgid());
        return 0;
    }
    if (argc != 3) return 64;
    if (!strcmp(argv[1], "--list")) {
        DIR *directory = opendir(argv[2]);
        if (!directory) { perror(argv[2]); return 1; }
        struct dirent *entry;
        while ((entry = readdir(directory)))
            if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) puts(entry->d_name);
        closedir(directory);
        return 0;
    }
    if (strcmp(argv[1], "--read")) return 64;
    int fd = open(argv[2], O_RDONLY | O_NOFOLLOW);
    struct stat info;
    if (fd < 0) { perror(argv[2]); return 1; }
    if (fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_size > 2 * 1024 * 1024) {
        fputs("Expected a regular diagnostic file no larger than 2 MiB\n", stderr);
        close(fd); return 1;
    }
    char bytes[16384]; ssize_t count;
    while ((count = read(fd, bytes, sizeof(bytes))) > 0) {
        ssize_t offset = 0;
        while (offset < count) {
            ssize_t sent = write(STDOUT_FILENO, bytes + offset, (size_t)(count - offset));
            if (sent < 0 && errno == EINTR) continue;
            if (sent <= 0) { close(fd); return 1; }
            offset += sent;
        }
    }
    close(fd);
    return count < 0 ? 1 : 0;
}
